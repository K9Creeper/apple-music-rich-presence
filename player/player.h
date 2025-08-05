#pragma once
#include "player-types.h"

#include <condition_variable>
#include <mutex>

class Player : public std::enable_shared_from_this<Player> {
	private:
		winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_smtcManager{ nullptr };

		std::shared_ptr<PlayerInfo> m_currentTrack;
		std::mutex m_trackMutex;

		PlayerInfoHandler m_playerHandler;

	private:
		void SCMTC_ProcessSession(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession session);
		void OnPlaybackInfoChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender, winrt::Windows::Foundation::IInspectable const&);
		void OnMediaPropertiesChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender, winrt::Windows::Foundation::IInspectable const&);

	public:
		std::mutex m_cvMutex;
		std::condition_variable m_cv;
		bool m_sessionAttached = false;

		static Player* s_instance;
	public:
		Player() = default;
		~Player();

		void Initialize();
		void SetPlayerInfoHandler(PlayerInfoHandler handler);
		std::shared_ptr<PlayerInfo> GetCurrentTrack();
		void ForceUpdate(PlayerForceUpdateFlags flags = PlayerForceUpdateFlags::None);
};