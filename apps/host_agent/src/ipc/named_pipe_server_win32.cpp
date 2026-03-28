#include "host_agent/ipc/named_pipe_server.h"

#include <windows.h>

#include <iostream>
#include <string>

namespace host_agent::ipc {

namespace {

std::wstring BuildFullPipeName(const std::wstring& short_name) {
    if (short_name.rfind(L"\\\\.\\pipe\\", 0) == 0) {
        return short_name;
    }
    return L"\\\\.\\pipe\\" + short_name;
}

void DispatchCompleteLines(std::string& pending, const MessageHandler& handler) {
    std::size_t newline = pending.find('\n');
    while (newline != std::string::npos) {
        std::string line = pending.substr(0, newline);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            handler(line);
        }
        pending.erase(0, newline + 1);
        newline = pending.find('\n');
    }
}

}  // namespace

NamedPipeServer::NamedPipeServer(std::wstring pipe_name)
    : full_pipe_name_(BuildFullPipeName(pipe_name)) {}

bool NamedPipeServer::Run(const MessageHandler& handler) {
    running_ = true;

    while (running_) {
        HANDLE pipe = CreateNamedPipeW(
            full_pipe_name_.c_str(),
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            64 * 1024,
            64 * 1024,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            std::cerr << "CreateNamedPipeW failed: " << GetLastError() << "\n";
            return false;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (!connected) {
            std::cerr << "ConnectNamedPipe failed: " << GetLastError() << "\n";
            CloseHandle(pipe);
            continue;
        }

        std::string pending;
        char buffer[4096];
        DWORD bytes_read = 0;

        while (running_) {
            const BOOL ok = ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr);
            if (!ok || bytes_read == 0) {
                break;
            }

            pending.append(buffer, bytes_read);
            DispatchCompleteLines(pending, handler);
        }

        if (!pending.empty()) {
            if (!pending.empty() && pending.back() == '\r') {
                pending.pop_back();
            }
            if (!pending.empty()) {
                handler(pending);
            }
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    return true;
}

void NamedPipeServer::Stop() {
    running_ = false;
}

}  // namespace host_agent::ipc
