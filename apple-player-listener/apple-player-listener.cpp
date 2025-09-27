#include "apple-player-listener.h"

#include <Windows.h>

#include <winrt/Windows.Foundation.Collections.h>

#include "../macros.h"

extern HWND hwnd;

bool ApplePlayerListener::CheckForAppleMusicSession(void)
{
    if (m_smtcManager) {
        for (auto const& session : m_smtcManager.GetSessions()) {
            auto appId = session.SourceAppUserModelId();
            if (std::wstring(appId.c_str()).find(APPLE_MUSIC_AUMID) != std::wstring::npos) {
                m_currentSession = session;
                m_currentSession.PlaybackInfoChanged([this](auto&& sender, auto&& args) {
                    OnChangeStub(sender, args);
                    });

                m_currentSession.MediaPropertiesChanged([this](auto&& sender, auto&& args) {
                    OnChangeStub(sender, args);
                    });

                return true;
            }
        }
    }
    return false;
}

void ApplePlayerListener::OnChangeStub(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender, winrt::Windows::Foundation::IInspectable const&){
    PostMessage(hwnd, WM_PLAYER_UPDATE, 0, 0);
}

void ApplePlayerListener::Initialize(void) {
    m_smtcManager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

    m_smtcManager.SessionsChanged([this](auto&&...) {
        this->CheckForAppleMusicSession();
    });

    if(CheckForAppleMusicSession())
        ApplePlayerListener::OnChangeStub(m_currentSession, nullptr);
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

    wchar_t hostName[256];
    wchar_t urlPath[1024];
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = _countof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = _countof(urlPath);

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        OutputDebugStringA("Failed to crack URL\n");
        return result;
    }

    HINTERNET hSession = WinHttpOpen(L"AppleMusicClient/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession, std::wstring(hostName, urlComp.dwHostNameLength).c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", std::wstring(urlPath, urlComp.dwUrlPathLength).c_str(), NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0,
        0, 0);

    if (bResults)
        bResults = WinHttpReceiveResponse(hRequest, NULL);

    if (bResults) {
        DWORD dwSize = 0;
        do {
            DWORD dwDownloaded = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);

            if (dwSize == 0)
                break;

            std::vector<char> buffer(dwSize + 1);
            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                buffer[dwDownloaded] = 0;
                result.append(buffer.data());
            }
        } while (dwSize > 0);
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
    escaped << std::hex;

    for (const auto& c : value) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        }
        else if (c == ' ') {
            escaped << '+';
        }
        else {
            escaped << '%' << std::setw(2) << std::uppercase << int((unsigned char)c);
        }
    }
    return escaped.str();
}

bool ApplePlayerInfo::UpdateUrls() {
    std::string artistUtf8 = WideToUTF8(artist);
    std::string albumUtf8 = WideToUTF8(albumTitle);

    std::string searchTerm = artistUtf8 + " " + albumUtf8;
    std::string encodedTerm = UrlEncode(searchTerm);

    const std::string& jsonUrl = "https://itunes.apple.com/search?term=" + encodedTerm + "&entity=album&limit=1";

    auto jsonResponse = HttpGet(std::wstring(jsonUrl.begin(), jsonUrl.end()));

    try {
        auto json = nlohmann::json::parse(jsonResponse);

        if (json.contains("resultCount") && json["resultCount"].get<int>() > 0) {
            auto& results = json["results"];
            if (!results.empty()) {
                if (results[0].contains("artworkUrl100")) {
                    std::string artworkUrl = results[0]["artworkUrl100"].get<std::string>();
                    thumbnailUrl = artworkUrl;
                }
                if (results[0].contains("collectionViewUrl")) {
                    std::string collectionUrl = results[0]["collectionViewUrl"].get<std::string>();
                    albumUrl = collectionUrl;
                }
            }
        }
    }
    catch (const std::exception& e) {
        OutputDebugStringA(("JSON parse error: " + std::string(e.what()) + "\n").c_str());
        return false;
    }

    return true;
}