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
static std::shared_ptr<DiscordIPC> discordIpc;

HWND hwnd{};
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static ApplePlayerInfo lastInfo;

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

json BuildActivityPayload(const ApplePlayerInfo& info)
{
    const int type = 2; // Discord custom media type
    auto now = std::chrono::system_clock::now();
    int64_t nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    int64_t posSeconds = std::chrono::duration_cast<std::chrono::seconds>(info.position).count();
    int64_t durSeconds = std::chrono::duration_cast<std::chrono::seconds>(info.duration).count();
    OutputDebugStringA(std::to_string(posSeconds).c_str());
    OutputDebugStringA("\n");
    OutputDebugStringA(std::to_string(durSeconds).c_str());
    OutputDebugStringA("\n");

    // --- Base Activity ---
    json activity = {
        {"type", type},
        {"details", !info.title.empty() ? WideToUTF8(info.title) : "Unknown Track"},
        {"state",   !info.artist.empty() ? WideToUTF8(info.artist) : "Unknown Artist"},
        {"assets",  json::object()},
        {"buttons", json::array({
            {
                {"label", "Play on Music"},
                {"url", (info.albumUrl && !info.albumUrl->empty()) ? *info.albumUrl : APPLE_MUSIC_URL}
            }
        })}
    };

    // Album name on hover
    if (!info.albumTitle.empty()) {
        activity["assets"]["large_text"] = WideToUTF8(info.albumTitle);
    }

    // Album art (or fallback)
    activity["assets"]["large_image"] =
        (info.thumbnailUrl && !info.thumbnailUrl->empty()) ? *info.thumbnailUrl : "apple_music_logo";

    // Playback state handling
    using PlaybackStatus = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
    
    switch (info.playbackStatus) {
    case PlaybackStatus::Paused:
        activity["state"] = "Paused | " + WideToUTF8(info.artist);
        if (activity.contains("timestamps"))
            activity.erase("timestamps");
        break;

    case PlaybackStatus::Playing:
    {
        posSeconds = std::max<int64_t>(0, posSeconds);
        durSeconds = std::max<int64_t>(1, durSeconds);

        int64_t startTime = nowSeconds - posSeconds;
        int64_t endTime = startTime + durSeconds;

        activity["timestamps"] = {
            {"start", startTime},
            {"end",   endTime}
        };
        break;
    }

    default: // Stopped or unknown
        if (activity.contains("timestamps"))
            activity.erase("timestamps");
        break;
    }

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
        if (applePlayer->IsAppleMusicAttached() && discordIpc->IsConnected()) {
            std::unique_ptr<ApplePlayerInfo> info{ applePlayer->ProcessSession() };
            if (info && info->isValid()) {
                if (info->title != lastInfo.title ||
                    info->artist != lastInfo.artist ||
                    info->playbackStatus != lastInfo.playbackStatus)
                {

                    OutputDebugStringA("Making payload\n");
                    auto payload = BuildActivityPayload(*info);
                    OutputDebugStringA("Finished payload\n");
                    discordIpc->SendActivity(payload);
                    lastInfo = *info;
                }
            }
        }

        if (!applePlayer->IsAppleMusicAttached() && discordIpc->IsConnected()) {
            discordIpc->Close();
        }
    }
    break;

    case WM_TIMER: {
        if (!applePlayer->IsAppleMusicAttached()) {
            applePlayer->Initialize();
        }

        if (applePlayer->IsAppleMusicAttached() && !discordIpc->IsConnected()) {
            discordIpc->Connect();
        }

        if (!applePlayer->IsAppleMusicAttached() && discordIpc->IsConnected()) {
            discordIpc->Close();
        }

        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_EXIT) {
            DestroyWindow(hwnd);
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}