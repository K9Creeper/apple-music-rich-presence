#include "discord-ipc.h"
#include <sstream>
#include <iostream>

using json = nlohmann::json;

DiscordIPC::DiscordIPC(const std::string& clientId)
    : clientId_(clientId), pipe_(INVALID_HANDLE_VALUE) {
}

DiscordIPC::~DiscordIPC() {
    Close();
}

bool DiscordIPC::Connect() {
    for (int i = 0; i < 10; ++i) {
        std::string pipeName = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        pipe_ = CreateFileA(pipeName.c_str(), GENERIC_WRITE | GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            OutputDebugStringA(("Connected to " + pipeName + "\n").c_str());
            return SendHandshake();
        }
    }
    OutputDebugStringA("Failed to connect to any Discord IPC pipe.\n");
    return false;
}

void DiscordIPC::Close() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool DiscordIPC::SendHandshake() {
    json payload = {
        {"v", 1},
        {"client_id", clientId_}
    };
    return SendFrame(0, payload); // Opcode 0 = HANDSHAKE
}

bool DiscordIPC::SendActivity(const json& activity) {
    json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"args", {
            {"activity", activity},
            {"pid", static_cast<int>(GetCurrentProcessId())}
        }},
        {"nonce", std::to_string(GetTickCount64())}
    };
    return SendFrame(1, payload); // Opcode 1 = FRAME
}

bool DiscordIPC::SendFrame(int opcode, const json& payload) {
    std::string data = payload.dump();
    int32_t length = static_cast<int32_t>(data.size());

    DWORD written;
    if (!WriteFile(pipe_, &opcode, 4, &written, nullptr)) return false;
    if (!WriteFile(pipe_, &length, 4, &written, nullptr)) return false;
    if (!WriteFile(pipe_, data.data(), data.size(), &written, nullptr)) return false;

    return true;
}