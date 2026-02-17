#pragma once
#include <windows.h>

#define WM_PLAYER_UPDATE (WM_APP + 1)

static inline std::string UrlEncodeUtf8(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;

    for (unsigned char c : value) {
        if (isalnum(c) || c == '.' || c == '-' || c == '_' || c == '~') {
            escaped << c;
        }
        else {
            escaped << '%' << std::setw(2) << int(c);
        }
    }
    return escaped.str();
}

static inline std::string WideToUTF8(const std::wstring& wide) {
    if (wide.empty()) return {};

    int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) return {};

    std::string utf8(utf8Size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8Size, nullptr, nullptr);

    return utf8;
}

static inline std::wstring UTF8ToWide(const std::string& utf8)
{
    if (utf8.empty()) return {};

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
        (int)utf8.size(), nullptr, 0);

    std::wstring wide(size_needed, 0);

    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
        &wide[0], size_needed);

    return wide;
}