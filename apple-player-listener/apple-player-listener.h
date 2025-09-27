#pragma once

#include <windows.h>

#include "apple-player-info.h"

#define APPLE_MUSIC_AUMID L"AppleInc.AppleMusic"

#define APPLLE_MUSIC_URL "https://music.apple.com/"

class ApplePlayerListener {
public:
	ApplePlayerListener() = default;
	~ApplePlayerListener();

private:
	winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager m_smtcManager{ nullptr };
	winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession m_currentSession{ nullptr };

	bool CheckForAppleMusicSession(bool attach = false);
	static void OnChangeStub(winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession sender, winrt::Windows::Foundation::IInspectable const&);

public:
	void Initialize();
	bool IsAppleMusicAttached() { return CheckForAppleMusicSession(); }

	ApplePlayerInfo* ProcessSession(void) const;
};