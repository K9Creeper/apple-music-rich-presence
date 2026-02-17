#include "discord-ipc.h"
#include <chrono>
#include <thread>
#include <sstream>
#include <iostream>

DiscordIPC::DiscordIPC(const std::string& clientId)
    : clientId_(clientId), pipe_(INVALID_HANDLE_VALUE) {
    lastUpdate_ = std::chrono::steady_clock::now() - std::chrono::seconds(30);
}

DiscordIPC::~DiscordIPC() {
    Close();
}

bool DiscordIPC::Connect(uint16_t ms_delay) {
    for (uint16_t i = 0; i < 10; ++i) {
        std::string pipeName = DISCORD_IPC_STRING + std::to_string(i);
        pipe_ = CreateFileA(pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr);

        if (pipe_ != INVALID_HANDLE_VALUE) {
            if (!SendHandshake()) {
                Close();
                return false;
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_delay));
    }
    return false;
}

void DiscordIPC::Close() {
    std::lock_guard<std::mutex> lock(pipeMutex_);
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe_, nullptr);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool DiscordIPC::IsConnected() const {
    return pipe_ != INVALID_HANDLE_VALUE;
}

bool DiscordIPC::EnsureConnected() {
    if (!IsConnected()) {
        return Connect();
    }
    return true;
}

bool DiscordIPC::SendHandshake() {
    json payload = { {"v", 1}, {"client_id", clientId_} };
    return SendFrame(DISCORD_IPC_OPCODE_HANDSHAKE, payload);
}

bool DiscordIPC::SendActivity(const json& activity) {
    auto now = std::chrono::steady_clock::now();
    if (now - lastUpdate_ < std::chrono::seconds(15)) {
        return false;
    }
    lastUpdate_ = now;

    if (!EnsureConnected()) return false;

    uint8_t bytes[16] = { 0 };

    uint64_t tick = GetTickCount64();
    std::memcpy(bytes, &tick, sizeof(tick));
    for (int i = 8; i < 16; ++i) {
        bytes[i] = static_cast<uint8_t>(tick >> ((i - 8) * 8));
    }
    std::ostringstream nonce;
    nonce << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        nonce << std::setw(2) << static_cast<int>(bytes[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) nonce << "-";
    }

    json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"args", {{"activity", activity}, {"pid", static_cast<int>(GetCurrentProcessId())}}},
        {"nonce", nonce.str() }
    };

    return SendFrame(DISCORD_IPC_OPCODE_FRAME, payload);
}

bool DiscordIPC::SendFrame(int opcode, const json& payload) {
    std::string data = payload.dump();
    int32_t length = static_cast<int32_t>(data.size());

    auto isDisconnectError = [](DWORD err) {
        return err == ERROR_BROKEN_PIPE ||
            err == ERROR_PIPE_NOT_CONNECTED ||
            err == ERROR_NO_DATA;
        };

    DWORD written = 0;

    std::lock_guard<std::mutex> lock(pipeMutex_);

    if (!WriteFile(pipe_, &opcode, sizeof(opcode), &written, nullptr) || written != sizeof(opcode)) {
        if (isDisconnectError(GetLastError())) Close();
        return false;
    }
    if (!WriteFile(pipe_, &length, sizeof(length), &written, nullptr) || written != sizeof(length)) {
        if (isDisconnectError(GetLastError())) Close();
        return false;
    }
    if (!WriteFile(pipe_, data.data(), data.size(), &written, nullptr) || written != data.size()) {
        if (isDisconnectError(GetLastError())) Close();
        return false;
    }

    char header[8];
    DWORD read = 0;
    if (!ReadFile(pipe_, header, sizeof(header), &read, nullptr) || read != 8) {
        if (isDisconnectError(GetLastError())) Close();
        return false;
    }

    int32_t respOp = *reinterpret_cast<int32_t*>(header);
    int32_t respLen = *reinterpret_cast<int32_t*>(header + 4);

    if (respLen > 0) {
        std::string response(respLen, '\0');
        if (!ReadFile(pipe_, response.data(), respLen, &read, nullptr) || read != respLen) {
            if (isDisconnectError(GetLastError())) Close();
            return false;
        }
    }

    return true;
}