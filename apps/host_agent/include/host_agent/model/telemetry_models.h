#ifndef HOST_AGENT_MODEL_TELEMETRY_MODELS_H
#define HOST_AGENT_MODEL_TELEMETRY_MODELS_H

#include <filesystem>
#include <string>

namespace host_agent::model {

struct ApiConfig {
    std::string bind = "127.0.0.1";
    int port = 8089;
};

struct GpuCollectorConfig {
    bool enabled = true;
    int sample_interval_ms = 500;
};

struct OllamaCollectorConfig {
    bool enabled = true;
    std::string base_url = "http://127.0.0.1:11434";
    int poll_interval_ms = 1000;
};

struct PushConfig {
    std::string url;  // empty = disabled
    int interval_ms = 5000;
};

struct ServiceConfig {
    std::wstring pipe_name = L"ollama_host_agent_lhm";
    std::filesystem::path schema_path = "apps/host_agent/sql/schema.sql";
    std::filesystem::path sqlite_path = "data/runtime/host_agent.db";
    std::string machine_id = "local-machine";
    ApiConfig api;
    GpuCollectorConfig gpu;
    OllamaCollectorConfig ollama;
    PushConfig push;
};

struct CollectorHealthUpdate {
    std::string source;
    std::string status;
    std::string detail;
};

}  // namespace host_agent::model

#endif  // HOST_AGENT_MODEL_TELEMETRY_MODELS_H
