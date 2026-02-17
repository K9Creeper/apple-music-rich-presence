#pragma once

#include <windows.h>
#include <thread>
#include <mutex>

#include "apple-player-info.h"

#define APPLE_MUSIC_AUMID L"AppleInc.AppleMusic"

#define APPLE_MUSIC_URL "https://music.apple.com/"

class ApplePlayerListener {
public:
	ApplePlayerListener() = default;
	~ApplePlayerListener();

private:
	winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_smtcManager{ nullptr };
	winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession m_currentSession{ nullptr };

	bool CheckForAppleMusicSession(bool attach = false);
	static void OnChangeStub(ApplePlayerListener* list, winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender, winrt::Windows::Foundation::IInspectable const&);

public:
	void Initialize();
	bool IsAppleMusicAttached() { return CheckForAppleMusicSession(); }

	ApplePlayerInfo* ProcessSession(void) const;

	winrt::hstring m_lastTrackId;
	std::chrono::seconds m_lastPosition;
	std::mutex m_updateMutex;
};