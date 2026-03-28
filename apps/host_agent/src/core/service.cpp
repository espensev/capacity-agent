#include "host_agent/core/service.h"

#include "host_agent/core/bridge_message_parser.h"

#include <iostream>
#include <string>
#include <utility>
#include <variant>

namespace host_agent::core {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

}  // namespace

Service::Service(model::ServiceConfig config)
    : config_(std::move(config)),
      store_(config_.sqlite_path, config_.schema_path),
      pipe_server_(config_.pipe_name),
      http_server_(config_.api, config_.machine_id, store_) {}

bool Service::Run() {
    if (!store_.EnsureSchema()) {
        std::cerr << "Failed to initialize SQLite schema from "
                  << config_.schema_path.string() << "\n";
        return false;
    }

    std::wcout << L"host_agent listening on pipe: " << config_.pipe_name << L"\n";
    std::cout << "SQLite path: " << config_.sqlite_path.string() << "\n";
    std::cout << "Machine ID: " << config_.machine_id << "\n";
    std::cout << "API: http://" << config_.api.bind << ":" << config_.api.port << "\n";

    running_.store(true);

    // Start HTTP server on its own thread
    http_server_.Start();

    // Start GPU collector if enabled
    if (config_.gpu.enabled) {
        gpu_collector_ = std::make_unique<collectors::DirectGpuCollector>(
            config_.gpu, config_.machine_id, store_);
        if (gpu_collector_->Initialize()) {
            gpu_collector_->Start();
            std::cout << "GPU collector started (interval: "
                      << config_.gpu.sample_interval_ms << "ms)\n";
        } else {
            std::cout << "GPU collector failed to initialize, continuing without it\n";
            gpu_collector_.reset();
        }
    }

    // Start Ollama collector if enabled
    if (config_.ollama.enabled) {
        ollama_collector_ = std::make_unique<collectors::OllamaRuntimeCollector>(
            config_.ollama, config_.machine_id, store_);
        if (ollama_collector_->Initialize()) {
            ollama_collector_->Start();
            std::cout << "Ollama collector started (interval: "
                      << config_.ollama.poll_interval_ms << "ms, url: "
                      << config_.ollama.base_url << ")\n";
        } else {
            std::cout << "Ollama collector failed to initialize, continuing without it\n";
            ollama_collector_.reset();
        }
    }

    // Start snapshot pusher if configured
    if (!config_.push.url.empty()) {
        snapshot_pusher_ = std::make_unique<SnapshotPusher>(config_.push, config_.api);
        snapshot_pusher_->Start();
        std::cout << "Snapshot pusher started (url: " << config_.push.url
                  << ", interval: " << config_.push.interval_ms << "ms)\n";
    }

    // Run pipe server on its own thread (non-blocking)
    pipe_thread_ = std::thread([this]() {
        pipe_server_.Run([this](std::string_view message) {
            HandlePipeMessage(message);
        });
    });

    std::cout << "host_agent running. Ctrl+C to stop.\n";

    // Main thread waits for shutdown signal
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    // Stop everything
    pipe_server_.Stop();
    if (pipe_thread_.joinable()) pipe_thread_.join();
    if (snapshot_pusher_) snapshot_pusher_->Stop();
    if (gpu_collector_) gpu_collector_->Stop();
    if (ollama_collector_) ollama_collector_->Stop();
    http_server_.Stop();

    std::cout << "host_agent stopped.\n";
    return true;
}

void Service::Stop() {
    running_.store(false);
    pipe_server_.Stop();
    if (snapshot_pusher_) snapshot_pusher_->Stop();
    if (gpu_collector_) gpu_collector_->Stop();
    if (ollama_collector_) ollama_collector_->Stop();
    http_server_.Stop();
}

void Service::HandlePipeMessage(std::string_view message) {
    model::BridgeMessage parsed_message;
    std::string parse_error;
    if (!ParseBridgeMessage(message, &parsed_message, &parse_error)) {
        std::cerr << "Discarding invalid bridge message: " << parse_error << "\n";
        return;
    }

    const bool ok = std::visit(
        Overloaded{
            [this](const model::HelloMessage& hello) {
                return store_.UpsertHello(hello);
            },
            [this](const model::CatalogSnapshotMessage& snapshot) {
                return store_.UpsertCatalogSnapshot(snapshot);
            },
            [this](const model::SampleBatchMessage& batch) {
                return store_.InsertSampleBatch(batch);
            },
            [this](const model::HeartbeatMessage& heartbeat) {
                return store_.UpsertHeartbeat(heartbeat);
            }},
        parsed_message);

    if (!ok) {
        std::cerr << "Failed to persist bridge message.\n";
    }
}

}  // namespace host_agent::core
