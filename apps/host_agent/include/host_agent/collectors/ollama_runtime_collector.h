#ifndef HOST_AGENT_COLLECTORS_OLLAMA_RUNTIME_COLLECTOR_H
#define HOST_AGENT_COLLECTORS_OLLAMA_RUNTIME_COLLECTOR_H

#include "host_agent/model/telemetry_models.h"
#include "host_agent/storage/sqlite_store.h"

#include <atomic>
#include <string>
#include <thread>

namespace host_agent::collectors {

class OllamaRuntimeCollector {
public:
    OllamaRuntimeCollector(const model::OllamaCollectorConfig& config,
                           const std::string& machine_id,
                           storage::SqliteStore& store);
    ~OllamaRuntimeCollector();

    OllamaRuntimeCollector(const OllamaRuntimeCollector&) = delete;
    OllamaRuntimeCollector& operator=(const OllamaRuntimeCollector&) = delete;

    bool Initialize();
    void Start();
    void Stop();
    std::string Name() const;

private:
    void PollLoop();
    bool PollOnce();

    model::OllamaCollectorConfig config_;
    std::string machine_id_;
    storage::SqliteStore& store_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace host_agent::collectors

#endif  // HOST_AGENT_COLLECTORS_OLLAMA_RUNTIME_COLLECTOR_H
