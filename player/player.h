#pragma once
#include "player-types.h"

#include <condition_variable>
#include <mutex>

class Player {
	private:
		winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_smtcManager{ nullptr };

		std::mutex m_trackMutex;
		std::shared_ptr<PlayerInfo> m_currentTrack;

		PlayerInfoHandler m_playerHandler;

	private:
		void SCMTC_ProcessSession(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession session);
		void OnPlaybackInfoChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender, winrt::Windows::Foundation::IInspectable const&);
		void OnMediaPropertiesChanged(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender, winrt::Windows::Foundation::IInspectable const&);

		bool CheckForAppleMusicSession();
		bool HandleSessionsChanged();

	public:

		std::mutex m_cvMutex;
		std::condition_variable m_cv;
		std::atomic<bool> m_sessionAttached{ false };


	public:
		Player() = default;
		~Player();

		void Initialize();
		void SetPlayerInfoHandler(PlayerInfoHandler handler);
		bool isValidTrack();
		PlayerInfo ForceUpdate(PlayerForceUpdateFlags flags = PlayerForceUpdateFlags::None, bool callHandler = true);
};