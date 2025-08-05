#include "discord-ipc.h"
#include <sstream>
#include <iostream>
#include <thread>

// OP CODES
#define HANDSHAKE 0
#define FRAME 1

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
            if (!SendHandshake()) {
                Close();
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            return true;
        }
    }
    OutputDebugStringA("Failed to connect to any Discord IPC pipe.\n");
    return false;
}

void DiscordIPC::Close() {
    listening.store(false);
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe_, nullptr);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool DiscordIPC::SendHandshake() {
    json payload = {
        {"v", 1},
        {"client_id", clientId_}
    };
    return SendFrame(HANDSHAKE, payload);
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
    return SendFrame(FRAME, payload);
}

bool DiscordIPC::IsConnected() const {
    return (pipe_ != INVALID_HANDLE_VALUE);
}

bool DiscordIPC::SendFrame(int opcode, const json& payload) {
    std::string data = payload.dump();
    int32_t length = static_cast<int32_t>(data.size());

    auto isDisconnectError = [](DWORD err) {
        return err == ERROR_BROKEN_PIPE ||
            err == ERROR_PIPE_NOT_CONNECTED ||
            err == ERROR_NO_DATA;
        };

    DWORD written;

    {
        std::lock_guard<std::mutex> lock(pipeMutex_);
        if (!WriteFile(pipe_, &opcode, sizeof(opcode), &written, nullptr) || written != sizeof(opcode)) {
            DWORD err = GetLastError();
            if (isDisconnectError(err)) Close();
                return false;
        }

        if (!WriteFile(pipe_, &length, sizeof(length), &written, nullptr) || written != sizeof(length)) {
            DWORD err = GetLastError();
            if (isDisconnectError(err)) Close();
                return false;
            }

        if (!WriteFile(pipe_, data.data(), data.size(), &written, nullptr) || written != data.size()) {
            DWORD err = GetLastError();
            if (isDisconnectError(err)) Close();
                return false;
            }
        }

        char header[8];
        DWORD read = 0;

        if (!ReadFile(pipe_, header, sizeof(header), &read, nullptr) || read != 8) {
            DWORD err = GetLastError();
            OutputDebugStringA(("Failed to read header: " + std::to_string(err) + "\n").c_str());
            if (isDisconnectError(err)) Close();
            return false;
        }

        int32_t respOp = *reinterpret_cast<int32_t*>(header);
        int32_t respLen = *reinterpret_cast<int32_t*>(header + 4);

        std::string responseBuf(respLen, '\0');
        if (!ReadFile(pipe_, responseBuf.data(), respLen, &read, nullptr) || read != respLen) {
            DWORD err = GetLastError();
            OutputDebugStringA(("Failed to read response: " + std::to_string(err) + "\n").c_str());
            if (isDisconnectError(err)) Close();
            return false;
        }

    return true;
}