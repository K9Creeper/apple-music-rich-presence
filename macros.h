#pragma once
#include <windows.h>

#define WM_PLAYER_UPDATE (WM_APP + 1)

static inline std::string WideToUTF8(const std::wstring& wide) {
    if (wide.empty()) return {};

    int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) return {};

    std::string utf8(utf8Size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8Size, nullptr, nullptr);

    return utf8;
}