#include "player.h"

#include <nlohmann/json.hpp>

#include "../pch.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

static std::string WideToUTF8(const std::wstring& wide) {
    if (wide.empty()) return {};

    int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) return {};

    std::string utf8(utf8Size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8Size, nullptr, nullptr);

    return utf8;
}

void Player::SCMTC_ProcessSession(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession session)
{
    if (!session) {
        OutputDebugStringA("SCMTC_ProcessSession: session is null, skipping.\n");
        return;
    }

    try {

        {
            auto mediaProps = session.TryGetMediaPropertiesAsync().get();
            auto playbackInfo = session.GetPlaybackInfo();
            auto timelineProps = session.GetTimelineProperties();

            auto position = std::chrono::duration_cast<std::chrono::seconds>(timelineProps.Position());
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(timelineProps.EndTime() - timelineProps.StartTime());

            std::lock_guard<std::mutex> lock(m_trackMutex);

            if (!m_currentTrack) {
                m_currentTrack = std::make_shared<PlayerInfo>(mediaProps, playbackInfo, position, duration);
            }
            else {
                *m_currentTrack = PlayerInfo(mediaProps, playbackInfo, position, duration);
            }
        }

        m_sessionAttached.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(m_cvMutex);
            m_cv.notify_one();
        }
    }
    catch (const winrt::hresult_error& e) {
        OutputDebugStringA(("SCMTC_ProcessSession failed: " + std::string(winrt::to_string(e.message())) + "\n").c_str());
    }
}

void Player::OnPlaybackInfoChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender, winrt::Windows::Foundation::IInspectable const&)
{
    SCMTC_ProcessSession(sender);
}

void Player::OnMediaPropertiesChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender, winrt::Windows::Foundation::IInspectable const&)
{
    SCMTC_ProcessSession(sender);
}

bool Player::CheckForAppleMusicSession() {
    for (auto const& session : m_smtcManager.GetSessions()) {
        auto appId = session.SourceAppUserModelId();
        if (std::wstring(appId.c_str()).find(L"AppleInc.AppleMusic") != std::wstring::npos) {
            session.PlaybackInfoChanged({ this, &Player::OnPlaybackInfoChanged });
            session.MediaPropertiesChanged({ this, &Player::OnMediaPropertiesChanged });
            SCMTC_ProcessSession(session);
            OutputDebugStringA("Found a session\n");
            return true;
        }
    }
    OutputDebugStringA("Didn't find a session\n");
    return false;
}

void Player::HandleSessionsChanged() {
    if (!CheckForAppleMusicSession()) {
        {
            std::lock_guard<std::mutex> lock(m_trackMutex);
            m_currentTrack.reset();
        }

        m_sessionAttached.store(false, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_cvMutex);
            m_cv.notify_one();
        }

        OutputDebugStringA("Stopping..\n");
    }
}

void Player::Initialize() {
    m_smtcManager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

    m_smtcManager.SessionsChanged([this](auto&&...) {
        HandleSessionsChanged();
    });

    HandleSessionsChanged();
}

void Player::SetPlayerInfoHandler(PlayerInfoHandler handler) {
    m_playerHandler = std::move(handler);
}

std::shared_ptr<PlayerInfo> Player::GetCurrentTrack() { return m_currentTrack; }

void Player::ForceUpdate(PlayerForceUpdateFlags flags)
{
    if (!m_smtcManager) return;

    auto session = m_smtcManager.GetCurrentSession();
    if (!session) return;

    std::optional<winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties> mediaPropsOpt;
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties mediaProps{ nullptr };

    if (Any(flags, PlayerForceUpdateFlags::Title | PlayerForceUpdateFlags::Artist | PlayerForceUpdateFlags::Album | PlayerForceUpdateFlags::Thumbnail)) {
        try {
            mediaProps = session.TryGetMediaPropertiesAsync().get();
            mediaPropsOpt = mediaProps;
        }
        catch (const winrt::hresult_error& e) {
            OutputDebugStringA(("ForceUpdate: Failed to get media props: " + std::string(winrt::to_string(e.message())) + "\n").c_str());
        }
    }

    auto timelineProps = session.GetTimelineProperties();

    PlayerInfo trackCopy;
    {
        std::lock_guard<std::mutex> lock(m_trackMutex);
        if (!m_currentTrack) return;
        if (mediaPropsOpt.has_value()) {
            if (Any(flags, PlayerForceUpdateFlags::Title)) m_currentTrack->title = mediaProps.Title();
            if (Any(flags, PlayerForceUpdateFlags::Artist)) m_currentTrack->artist = mediaProps.Artist();
            if (Any(flags, PlayerForceUpdateFlags::Album)) m_currentTrack->albumTitle = mediaProps.AlbumTitle();
            
            m_currentTrack->CorrectDetails();
            if (Any(flags, PlayerForceUpdateFlags::Thumbnail)) {
                m_currentTrack->UpdateUrls();
            }
        }
        if (Any(flags, PlayerForceUpdateFlags::Position))
            m_currentTrack->position = std::chrono::duration_cast<std::chrono::seconds>(timelineProps.Position());
        if (Any(flags, PlayerForceUpdateFlags::Duration))
            m_currentTrack->duration = std::chrono::duration_cast<std::chrono::seconds>(timelineProps.EndTime() - timelineProps.StartTime());

        trackCopy = *m_currentTrack;
    }

    if (m_playerHandler) {
        m_playerHandler(trackCopy);
    }

}

Player::~Player() {
    
}

std::string HttpGet(const std::wstring& url) {
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

void PlayerInfo::UpdateUrls() {
    // Compose search term from artist + album
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
    }
}