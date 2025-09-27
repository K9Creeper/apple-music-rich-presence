#pragma once

#include <windows.h>
#include <string>
#include <mutex>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

enum DiscordIpcOpcode {
    DISCORD_IPC_OPCODE_HANDSHAKE = 0,
    DISCORD_IPC_OPCODE_FRAME = 1
};

#define DISCORD_IPC_STRING "\\\\.\\pipe\\discord-ipc-"

class DiscordIPC {
public:
    explicit DiscordIPC(const std::string& clientId);
    ~DiscordIPC(void);

private:
    HANDLE pipe_;
    std::string clientId_;
    std::mutex pipeMutex_;

    bool SendHandshake(void);
    bool SendFrame(int opcode, const json& payload);

public:

    bool Connect(uint16_t ms_delay = 1000U, uint16_t attempts = 1U);
    void Close(void);
    bool SendActivity(const json& activity);

    bool IsConnected(void) const;
};