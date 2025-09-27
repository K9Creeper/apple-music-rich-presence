#include "pch.h"

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h> 

#include "apple-player-listener/apple-player-listener.h"
#include "discord-ipc/discord-ipc.h"
#include "macros.h"

#define WM_TRAYICON (WM_USER + 1)
#define IDM_EXIT 1001

constexpr uint64_t clientId = 1402044057647186053;

static NOTIFYICONDATA nid = {};

static auto applePlayer = std::make_shared<ApplePlayerListener>();
static std::shared_ptr<DiscordIPC> discordIpc{ nullptr };

HWND hwnd{};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    discordIpc = std::make_shared<DiscordIPC>(std::to_string(clientId));

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AppleMusicDiscordRichPresenceAppClass";

    if (!RegisterClass(&wc)) return 1;

    hwnd = CreateWindowEx(
        0, wc.lpszClassName, L"Apple Music Discord Rich Presence App",
        0, 0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 1;

    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Apple Music Discord Rich Presence");

    Shell_NotifyIcon(NIM_ADD, &nid);

    applePlayer->Initialize();
    
    if (applePlayer->IsAppleMusicAttached()) {
        discordIpc->Connect();
    }

    SetTimer(hwnd, 1, 2500, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);
    winrt::uninit_apartment();

    return 0;
}

json BuildActivityPayload(const ApplePlayerInfo& info) {
    static ApplePlayerInfo lastInfo;
    const int type = 2;

    auto now = std::chrono::system_clock::now();
    auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    int64_t posSeconds = std::chrono::duration_cast<std::chrono::seconds>(info.position).count();
    int64_t durSeconds = std::chrono::duration_cast<std::chrono::seconds>(info.duration).count();

    json activity = {
        {"type", type},
        {"details", WideToUTF8(info.title)},
        {"state", WideToUTF8(info.artist)},
        {"assets", json::object()},
        {"buttons", json::array({
            {
                {"label", "Play on Music"},
                {"url", info.albumUrl.has_value() && !info.albumUrl->empty()
                    ? info.albumUrl.value()
                    : APPLLE_MUSIC_URL}
            }
        })}
    };

    if (!info.albumTitle.empty()) {
        activity["assets"]["large_text"] = WideToUTF8(info.albumTitle);
    }

    activity["assets"]["large_image"] =
        (info.thumbnailUrl.has_value() && !info.thumbnailUrl->empty())
        ? info.thumbnailUrl.value()
        : "apple_music_logo";

    if (info.playbackStatus == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused) {
        activity["state"] = "Paused | " + WideToUTF8(info.artist);
        activity.erase("timestamps");
    }
    else if (info.playbackStatus == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
        int64_t startTime = nowSeconds - posSeconds;
        int64_t endTime = startTime + durSeconds;

        activity["timestamps"] = {
            {"start", startTime},
            {"end", endTime}
        };
    }
    else {
        activity.erase("timestamps");
    }

    lastInfo = info;
    return activity;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            InsertMenu(hMenu, -1, MF_BYPOSITION, IDM_EXIT, L"Exit");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
        }
        break;

    case WM_PLAYER_UPDATE:
    {
        if (!applePlayer->IsAppleMusicAttached()) {
            discordIpc->Close();
            break;
        }

        std::unique_ptr<ApplePlayerInfo> info{ applePlayer->ProcessSession() };

        if (info && info->isValid()) {
            auto payload = BuildActivityPayload(*info);
            discordIpc->SendActivity(payload);
        }
    }
    break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_EXIT) {
            DestroyWindow(hwnd);
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;

    case WM_TIMER:
        if (!applePlayer->IsAppleMusicAttached()) {
            applePlayer->Initialize();
        }

        if (!applePlayer->IsAppleMusicAttached() && discordIpc->IsConnected()) {
            discordIpc->Close();
        }

        if (!discordIpc->IsConnected() && applePlayer->IsAppleMusicAttached()) {
            discordIpc->Connect();
        }

        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
