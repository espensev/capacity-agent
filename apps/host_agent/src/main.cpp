#include "host_agent/core/service.h"
#include "host_agent/model/telemetry_models.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

host_agent::core::Service* g_service = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
        std::cout << "\nShutdown signal received, stopping...\n";
        if (g_service != nullptr) {
            g_service->Stop();
        }
        return TRUE;
    }
    return FALSE;
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

void PrintUsage() {
    std::wcout
        << L"host_agent [--pipe-name <name>] [--schema-path <path>] [--db-path <path>]\n"
        << L"           [--machine-id <id>] [--api-bind <addr>] [--api-port <port>]\n"
        << L"           [--ollama-url <url>] [--ollama-poll-ms <ms>]\n"
        << L"           [--gpu-sample-ms <ms>] [--no-gpu] [--no-ollama]\n"
        << L"           [--push-url <url>] [--push-interval-ms <ms>]\n";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    host_agent::model::ServiceConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];

        if (arg == L"--help" || arg == L"-h") {
            PrintUsage();
            return 0;
        }

        // Boolean flags (no value required)
        if (arg == L"--no-gpu") {
            config.gpu.enabled = false;
            continue;
        }
        if (arg == L"--no-ollama") {
            config.ollama.enabled = false;
            continue;
        }

        if (i + 1 >= argc) {
            std::wcerr << L"Missing value for argument: " << arg << L"\n";
            PrintUsage();
            return 1;
        }

        const std::wstring value = argv[++i];
        if (arg == L"--pipe-name") {
            config.pipe_name = value;
        } else if (arg == L"--schema-path") {
            config.schema_path = std::filesystem::path(value);
        } else if (arg == L"--db-path") {
            config.sqlite_path = std::filesystem::path(value);
        } else if (arg == L"--machine-id") {
            config.machine_id = WideToUtf8(value);
        } else if (arg == L"--api-bind") {
            config.api.bind = WideToUtf8(value);
        } else if (arg == L"--api-port") {
            config.api.port = std::stoi(value);
        } else if (arg == L"--ollama-url") {
            config.ollama.base_url = WideToUtf8(value);
        } else if (arg == L"--ollama-poll-ms") {
            config.ollama.poll_interval_ms = std::stoi(value);
        } else if (arg == L"--gpu-sample-ms") {
            config.gpu.sample_interval_ms = std::stoi(value);
        } else if (arg == L"--push-url") {
            config.push.url = WideToUtf8(value);
        } else if (arg == L"--push-interval-ms") {
            config.push.interval_ms = std::stoi(value);
        } else {
            std::wcerr << L"Unknown argument: " << arg << L"\n";
            PrintUsage();
            return 1;
        }
    }

    host_agent::core::Service service(config);
    g_service = &service;
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    const int result = service.Run() ? 0 : 1;
    g_service = nullptr;
    return result;
}
