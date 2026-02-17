#pragma once
#include <windows.h>
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

constexpr int DISCORD_IPC_OPCODE_HANDSHAKE = 0;
constexpr int DISCORD_IPC_OPCODE_FRAME = 1;
constexpr int DISCORD_IPC_OPCODE_CLOSE = 2;
constexpr int DISCORD_IPC_OPCODE_PING = 3;
constexpr int DISCORD_IPC_OPCODE_PONG = 4;
const std::string DISCORD_IPC_STRING = R"(\\.\pipe\discord-ipc-)";

class DiscordIPC {
public:
    DiscordIPC(const std::string& clientId);
    ~DiscordIPC();

    bool Connect(uint16_t ms_delay = 100);
    void Close();
    bool SendActivity(const json& activity);
    bool IsConnected() const;

private:
    bool SendHandshake();
    bool SendFrame(int opcode, const json& payload);
    bool EnsureConnected();
    std::string ReadResponse();

    std::string clientId_;
    HANDLE pipe_;
    std::mutex pipeMutex_;
    std::chrono::steady_clock::time_point lastUpdate_;
};
