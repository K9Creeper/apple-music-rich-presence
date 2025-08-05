#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>

#include <functional>

enum PlayerForceUpdateFlags : uint32_t {
    None = 0,
    Title = 1 << 0,
    Artist = 1 << 1,
    Album = 1 << 2,
    Duration = 1 << 3,
    Position = 1 << 4,
    Thumbnail = 1 << 5,
    Status = 1 << 6
};

inline PlayerForceUpdateFlags operator|(PlayerForceUpdateFlags a, PlayerForceUpdateFlags b) {
    return static_cast<PlayerForceUpdateFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
        );
}

inline PlayerForceUpdateFlags operator&(PlayerForceUpdateFlags a, PlayerForceUpdateFlags b) {
    return static_cast<PlayerForceUpdateFlags>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
        );
}

inline PlayerForceUpdateFlags& operator|=(PlayerForceUpdateFlags& a, PlayerForceUpdateFlags b) {
    a = a | b;
    return a;
}

inline bool Any(PlayerForceUpdateFlags flags, PlayerForceUpdateFlags test) {
    return static_cast<uint32_t>(flags & test) != 0;
}

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Media::Control;

struct PlayerInfo
{
    std::wstring title;
    std::wstring artist;
    std::wstring albumTitle;

    std::chrono::seconds duration{};
    std::chrono::seconds position{};

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus playbackStatus;

    std::optional<std::string> thumbnailUrl;
    std::optional<std::string> albumUrl;

    PlayerInfo() = default;

    PlayerInfo(const winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties& mediaProps,
        const winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackInfo& playbackInfo,
        const std::chrono::seconds& pos,
        const std::chrono::seconds& dur)
        : title(mediaProps.Title().c_str()),
        artist(mediaProps.Artist().c_str()),
        albumTitle(mediaProps.AlbumTitle().c_str()),
        duration(dur),
        position(pos),
        playbackStatus(playbackInfo.PlaybackStatus())
    {
        
    }

    bool isValid() const {
        return !(duration.count() == 0 || title.empty() || artist.empty());
    }

    void CorrectDetails() {
        if (albumTitle.empty()) {
            size_t sepPos = artist.find(L" — ");
            if (sepPos != std::wstring::npos) {
                std::wstring newArtist = artist.substr(0, sepPos);
                std::wstring newAlbum = artist.substr(sepPos + 3);

                artist = newArtist;
                albumTitle = newAlbum;
            }
        }
	}

    void UpdateUrls();
};

using PlayerInfoHandler = std::function<void(const PlayerInfo& info)>;