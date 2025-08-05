#pragma once

#include <windows.h>
#include <string>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

class DiscordIPC {
public:
    explicit DiscordIPC(const std::string& clientId);
    ~DiscordIPC();

    bool Connect();
    void Close();
    bool SendActivity(const json& activity);

	bool IsConnected() const;

private:
    HANDLE pipe_;
    std::string clientId_;

    bool SendHandshake();
    bool SendFrame(int opcode, const json& payload);
};