#ifndef HOST_AGENT_CORE_SERVICE_H
#define HOST_AGENT_CORE_SERVICE_H

#include "host_agent/api/http_server.h"
#include "host_agent/collectors/direct_gpu_collector.h"
#include "host_agent/collectors/ollama_runtime_collector.h"
#include "host_agent/ipc/named_pipe_server.h"
#include "host_agent/model/telemetry_models.h"
#include "host_agent/storage/sqlite_store.h"

#include <atomic>
#include <memory>
#include <thread>

namespace host_agent::core {

class Service {
public:
    explicit Service(model::ServiceConfig config);

    bool Run();
    void Stop();

private:
    void HandlePipeMessage(std::string_view message);

    model::ServiceConfig config_;
    storage::SqliteStore store_;
    ipc::NamedPipeServer pipe_server_;
    api::HttpServer http_server_;
    std::unique_ptr<collectors::DirectGpuCollector> gpu_collector_;
    std::unique_ptr<collectors::OllamaRuntimeCollector> ollama_collector_;
    std::atomic<bool> running_{false};
};

}  // namespace host_agent::core

#endif  // HOST_AGENT_CORE_SERVICE_H

