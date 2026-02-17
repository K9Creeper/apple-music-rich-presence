#include "apple-player-listener.h"

#include <Windows.h>

#include <winrt/Windows.Foundation.Collections.h>

#include "../macros.h"

extern HWND hwnd;

bool ApplePlayerListener::CheckForAppleMusicSession(bool attach)
{
    if (m_smtcManager) {
        for (auto const& session : m_smtcManager.GetSessions()) {
            auto appId = session.SourceAppUserModelId();
            if (std::wstring(appId.c_str()).find(APPLE_MUSIC_AUMID) != std::wstring::npos) {
                if (attach) {
                    m_currentSession = session;
                    m_currentSession.PlaybackInfoChanged([this](auto&& sender, auto&& args) {
                        OnChangeStub(sender, args);
                        });

                    m_currentSession.MediaPropertiesChanged([this](auto&& sender, auto&& args) {
                        OnChangeStub(sender, args);
                        });
                }
                return true;
            }
        }
    }
    return false;
}

void ApplePlayerListener::OnChangeStub(ApplePlayerListener* list,
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender,
    winrt::Windows::Foundation::IInspectable const&)
{
    std::thread([list, sender]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        std::lock_guard<std::mutex> lock(list->m_updateMutex);

        try {
            auto mediaProps = sender.TryGetMediaPropertiesAsync().get();
            auto timelineProps = sender.GetTimelineProperties();

            auto position = std::chrono::duration_cast<std::chrono::seconds>(timelineProps.Position());
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(timelineProps.EndTime() - timelineProps.StartTime());

            winrt::hstring trackId = mediaProps.Title() + L"|" + mediaProps.AlbumArtist() + L"|" + mediaProps.AlbumTitle();

            if (trackId != list->m_lastTrackId || position != list->m_lastPosition) {
                list->m_lastTrackId = trackId;
                list->m_lastPosition = position;

                PostMessage(hwnd, WM_TIMER, 0, 0);
                PostMessage(hwnd, WM_PLAYER_UPDATE, 0, 0);
            }
        }
        catch (const winrt::hresult_error& ex) {
            OutputDebugStringW((L"Error reading Apple Music properties: " + std::to_wstring(ex.code()) + L" - " + ex.message() + L"\n").c_str());
        }

        }).detach();
}

void ApplePlayerListener::Initialize(void) {
    if (m_smtcManager == nullptr) {
        m_smtcManager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    }

    if (m_smtcManager) {
        m_smtcManager.SessionsChanged([this](auto&&...) {
            this->CheckForAppleMusicSession(true);
            });

        if (CheckForAppleMusicSession(true))
            ApplePlayerListener::OnChangeStub(m_currentSession, nullptr);
    }

}

ApplePlayerListener::~ApplePlayerListener() {
    if (m_currentSession) {
        m_currentSession = nullptr;
    }
    m_smtcManager = nullptr;
}

ApplePlayerInfo* ApplePlayerListener::ProcessSession(void) const {
    ApplePlayerInfo* trackInfo = nullptr;
    if (m_currentSession) {
        try {
            auto mediaProps = m_currentSession.TryGetMediaPropertiesAsync().get();
            auto playbackInfo = m_currentSession.GetPlaybackInfo();
            auto timelineProps = m_currentSession.GetTimelineProperties();

            auto position = std::chrono::duration_cast<std::chrono::seconds>(timelineProps.Position());
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(timelineProps.EndTime() - timelineProps.StartTime());

            trackInfo = new ApplePlayerInfo(mediaProps, playbackInfo, position, duration);
            trackInfo->CorrectDetails();
            trackInfo->UpdateUrls();
        }
        catch (const winrt::hresult_error& ex) {
            OutputDebugStringW((L"Error processing Apple Music session: " + std::to_wstring(ex.code()) + L" - " + ex.message() + L"\n").c_str());
        }
    }
	return trackInfo;
}

#include <sstream>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

static std::string HttpGet(const std::wstring& url) {
    std::string result;

    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);

    wchar_t hostName[256]{};
    wchar_t urlPath[1024]{};       // path only
    wchar_t extraInfo[2048]{};     // "?query....."

    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = _countof(hostName);

    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = _countof(urlPath);

    urlComp.lpszExtraInfo = extraInfo;
    urlComp.dwExtraInfoLength = _countof(extraInfo);

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        OutputDebugStringA("Failed to crack URL\n");
        return result;
    }

    std::wstring fullPath = std::wstring(urlPath, urlComp.dwUrlPathLength) +
        std::wstring(extraInfo, urlComp.dwExtraInfoLength);

    HINTERNET hSession = WinHttpOpen(L"AppleMusicClient/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(
        hSession,
        std::wstring(hostName, urlComp.dwHostNameLength).c_str(),
        urlComp.nPort,
        0);

    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        fullPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    BOOL bResults = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);

    if (bResults)
        bResults = WinHttpReceiveResponse(hRequest, nullptr);

    if (bResults) {
        DWORD dwSize = 0;
        do {
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0)
                break;

            std::vector<char> buffer(dwSize + 1);

            DWORD dwDownloaded = 0;
            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded))
                break;

            buffer[dwDownloaded] = 0;
            result.append(buffer.data(), dwDownloaded);

        } while (true);
    }
    else {
        OutputDebugStringA("WinHttp request failed\n");
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

static std::string UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '*') {
            escaped << c;
        }
        else if (c == ' ') {
            escaped << '+';
        }
        else {
            escaped << '%' << std::setw(2) << int(c);
        }
    }

    return escaped.str();
}

bool ApplePlayerInfo::UpdateUrls()
{
    albumUrl = APPLE_MUSIC_URL;
    thumbnailUrl = "apple_music_logo";
    return false;
}