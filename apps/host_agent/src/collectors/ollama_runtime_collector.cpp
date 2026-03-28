#include "host_agent/collectors/ollama_runtime_collector.h"

#include <httplib.h>
#include <cJSON.h>

#include <chrono>
#include <iostream>
#include <string>

namespace host_agent::collectors {

namespace {

std::int64_t NowUtcMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct CJsonDeleter {
    void operator()(cJSON* ptr) const {
        if (ptr) cJSON_Delete(ptr);
    }
};
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

// Parse host and port from a URL like "http://127.0.0.1:11434"
bool ParseHostPort(const std::string& url, std::string& host, int& port) {
    std::string rest = url;
    const auto scheme_end = rest.find("://");
    if (scheme_end != std::string::npos) {
        rest = rest.substr(scheme_end + 3);
    }
    if (!rest.empty() && rest.back() == '/') {
        rest.pop_back();
    }

    const auto colon = rest.rfind(':');
    if (colon != std::string::npos) {
        host = rest.substr(0, colon);
        port = std::stoi(rest.substr(colon + 1));
    } else {
        host = rest;
        port = 11434;
    }
    return !host.empty();
}

}  // namespace

OllamaRuntimeCollector::OllamaRuntimeCollector(
    const model::OllamaCollectorConfig& config,
    const std::string& machine_id,
    storage::SqliteStore& store)
    : config_(config),
      machine_id_(machine_id),
      store_(store) {}

OllamaRuntimeCollector::~OllamaRuntimeCollector() {
    Stop();
}

bool OllamaRuntimeCollector::Initialize() {
    std::string host;
    int port;
    if (!ParseHostPort(config_.base_url, host, port)) {
        std::cerr << "Ollama collector: invalid base_url: " << config_.base_url << "\n";
        return false;
    }

    httplib::Client client(host, port);
    client.set_connection_timeout(2);
    client.set_read_timeout(2);

    auto res = client.Get("/api/version");
    if (!res || res->status != 200) {
        std::cout << "Ollama collector: Ollama not reachable at " << config_.base_url
                  << " (will retry on each poll)\n";
    } else {
        std::cout << "Ollama collector: connected to " << config_.base_url << "\n";
    }

    return true;
}

void OllamaRuntimeCollector::Start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread([this]() { PollLoop(); });
}

void OllamaRuntimeCollector::Stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::string OllamaRuntimeCollector::Name() const {
    return "ollama_runtime_collector";
}

bool OllamaRuntimeCollector::PollOnce() {
    std::string host;
    int port;
    if (!ParseHostPort(config_.base_url, host, port)) return false;

    httplib::Client client(host, port);
    client.set_connection_timeout(3);
    client.set_read_timeout(5);

    auto res = client.Get("/api/ps");
    if (!res || res->status != 200) {
        store_.UpsertCollectorHealthDirect(
            machine_id_, "ollama", "ollama_poller", "unreachable", NowUtcMs(),
            res ? "HTTP " + std::to_string(res->status) : "connection failed");
        return false;
    }

    CJsonPtr json(cJSON_Parse(res->body.c_str()));
    if (!json) {
        store_.UpsertCollectorHealthDirect(
            machine_id_, "ollama", "ollama_poller", "error", NowUtcMs(), "invalid JSON");
        return false;
    }

    int loaded_model_count = 0;
    std::int64_t total_vram_bytes = 0;
    std::int64_t total_ram_bytes = 0;

    const cJSON* models = cJSON_GetObjectItem(json.get(), "models");
    if (cJSON_IsArray(models)) {
        loaded_model_count = cJSON_GetArraySize(models);

        const cJSON* model = nullptr;
        cJSON_ArrayForEach(model, models) {
            const cJSON* size_vram = cJSON_GetObjectItem(model, "size_vram");
            if (cJSON_IsNumber(size_vram)) {
                total_vram_bytes += static_cast<std::int64_t>(size_vram->valuedouble);
            }
            const cJSON* size = cJSON_GetObjectItem(model, "size");
            if (cJSON_IsNumber(size)) {
                const std::int64_t total_size = static_cast<std::int64_t>(size->valuedouble);
                const std::int64_t vram = size_vram && cJSON_IsNumber(size_vram)
                                              ? static_cast<std::int64_t>(size_vram->valuedouble)
                                              : 0;
                if (total_size > vram) {
                    total_ram_bytes += (total_size - vram);
                }
            }
        }
    }

    const std::int64_t now_ms = NowUtcMs();

    store_.InsertOllamaRuntimeSample(
        machine_id_, now_ms, config_.base_url,
        std::nullopt,
        std::nullopt,
        loaded_model_count,
        total_vram_bytes > 0 ? std::optional<std::int64_t>{total_vram_bytes} : std::nullopt,
        total_ram_bytes > 0 ? std::optional<std::int64_t>{total_ram_bytes} : std::nullopt);

    store_.UpsertCollectorHealthDirect(
        machine_id_, "ollama", "ollama_poller", "ok", now_ms, std::nullopt);

    return true;
}

void OllamaRuntimeCollector::PollLoop() {
    while (running_.load()) {
        const auto start = std::chrono::steady_clock::now();

        PollOnce();

        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto target = std::chrono::milliseconds(config_.poll_interval_ms);
        if (elapsed < target) {
            auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(target - elapsed);
            while (remaining_ms.count() > 0 && running_.load()) {
                auto chunk = std::min(remaining_ms, std::chrono::milliseconds(250));
                std::this_thread::sleep_for(chunk);
                remaining_ms -= chunk;
            }
        }
    }
}

}  // namespace host_agent::collectors
