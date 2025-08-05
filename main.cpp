#include "pch.h"

#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>

#include <nlohmann/json.hpp>

#include "discord-ipc/discord-ipc.h"
#include "player/player.h"

#include <winrt/Windows.Foundation.h>

#define WM_TRAYICON (WM_USER + 1)
#define IDM_EXIT 1001

// Globals
NOTIFYICONDATA nid = {};
std::atomic<bool> isRunning = false;
std::atomic<bool> isDone = false;

auto player = std::make_shared<Player>();

// Utility function
static std::string WideToUTF8(const std::wstring& wide) {
    if (wide.empty()) return {};

    int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) return {};

    std::string utf8(utf8Size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8Size, nullptr, nullptr);

    return utf8;
}

// Forward declarations
DWORD WINAPI Init(LPVOID);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Global IPC client pointer
std::unique_ptr<DiscordIPC> discordIpc;

// Main entry point
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    // Register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AppleMusicDiscordRichPresenceAppClass";

    if (!RegisterClass(&wc)) return 1;

    // Create hidden window
    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, L"Apple Music Discord Rich Presence App",
        0, 0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 1;

    // Setup tray icon
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Apple Music Discord Rich Presence");

    Shell_NotifyIcon(NIM_ADD, &nid);

    auto trayCleanup = [] {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        };

    std::thread workerThread([] {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        Init(nullptr);
        });

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Begin cleanup
    isRunning.store(false);
    if (workerThread.joinable()) workerThread.join();

    discordIpc.reset();

    trayCleanup();
    winrt::uninit_apartment();

    return 0;
}

json BuildActivityPayload(const PlayerInfo& info)
{
    auto now = std::chrono::system_clock::now();
    auto startTime = std::chrono::system_clock::to_time_t(now) - info.position.count();
    auto endTime = std::chrono::system_clock::to_time_t(now) + (info.duration - info.position).count();

    json activity = {
        {"type", 2},
        {"details", WideToUTF8(info.title)},
        {"state", WideToUTF8(info.artist)},
        {"assets", json::object()},
        // Not sure abt buttons
        {"buttons", json::array({
        {
                {"label", "Listen to Album"},
                {"url", ((info.albumUrl.has_value() && !info.albumUrl->empty()) ? info.albumUrl.value() : "https://music.apple.com/")}
        }})
        },
    };

    if (info.playbackStatus == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused) {
        activity["state"] = "Paused | " + WideToUTF8(info.artist);
    }

    if (!info.albumTitle.empty()) {
        activity["assets"]["large_text"] = WideToUTF8(info.albumTitle);
    }

    if (info.thumbnailUrl.has_value() && !info.thumbnailUrl->empty()) {
        activity["assets"]["large_image"] = info.thumbnailUrl.value();
    }
    else {
        activity["assets"]["large_image"] = "apple_music_logo";
    }

    if (info.playbackStatus == winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
        auto now = std::chrono::system_clock::now();
        auto startTime = std::chrono::system_clock::to_time_t(now) - info.position.count();
        auto endTime = std::chrono::system_clock::to_time_t(now) + (info.duration - info.position).count();

        activity["timestamps"] = {
            {"start", startTime},
            {"end", endTime}
        };
    }

    return activity;
}

// Background thread
DWORD WINAPI Init(LPVOID) {    
    const uint64_t clientId = 1402044057647186053;

    discordIpc = std::make_unique<DiscordIPC>(std::to_string(clientId));
    if (!discordIpc->Connect()) {
        OutputDebugStringA("Failed to connect to Discord IPC in Init.\n");
        return 1;
    }

    player->SetPlayerInfoHandler([&](const PlayerInfo& info) {
        if (!info.isValid() || !discordIpc) return;
            static std::string lastThumb;

            if (info.thumbnailUrl.has_value()) {
                if (info.thumbnailUrl.value() != lastThumb) {
                    lastThumb = info.thumbnailUrl.value();
                    player->ForceUpdate(PlayerForceUpdateFlags::Thumbnail);
                }
            }

            json activity = BuildActivityPayload(info);
            discordIpc->SendActivity(activity);
        });

    player->Initialize();

    int thumbnailUpdateCounter = 0;

    while (isRunning.load()) {
        {
            std::unique_lock<std::mutex> lock(player->m_cvMutex);
            player->m_cv.wait(lock, [] { return !isRunning.load() || player->m_sessionAttached; });
        }

        if (!isRunning.load()) break;

        PlayerForceUpdateFlags updateMask =
            PlayerForceUpdateFlags::Title |
            PlayerForceUpdateFlags::Artist |
            PlayerForceUpdateFlags::Album |
            PlayerForceUpdateFlags::Duration |
            PlayerForceUpdateFlags::Position |
            PlayerForceUpdateFlags::Status;

        if (thumbnailUpdateCounter <= 0) {
            updateMask = static_cast<PlayerForceUpdateFlags>(
                static_cast<uint32_t>(updateMask) | static_cast<uint32_t>(PlayerForceUpdateFlags::Thumbnail)
                );
            thumbnailUpdateCounter = 10; // refresh thumbnail every 10
        }
        else {
            thumbnailUpdateCounter--;
        }

        {
            std::lock_guard<std::mutex> lock(player->m_cvMutex);
            if (player->m_sessionAttached) {
                player->ForceUpdate(updateMask);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return 0;
}

// Window message handler
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

    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_EXIT) {
            isRunning.store(false);
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