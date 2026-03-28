#ifndef HOST_AGENT_COLLECTORS_DIRECT_GPU_COLLECTOR_H
#define HOST_AGENT_COLLECTORS_DIRECT_GPU_COLLECTOR_H

#include "host_agent/model/telemetry_models.h"
#include "host_agent/storage/sqlite_store.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class GpuSensorReader;

namespace host_agent::collectors {

class DirectGpuCollector {
public:
    DirectGpuCollector(const model::GpuCollectorConfig& config,
                       const std::string& machine_id,
                       storage::SqliteStore& store);
    ~DirectGpuCollector();

    DirectGpuCollector(const DirectGpuCollector&) = delete;
    DirectGpuCollector& operator=(const DirectGpuCollector&) = delete;

    bool Initialize();
    void Start();
    void Stop();
    std::string Name() const;

private:
    void PollLoop();

    model::GpuCollectorConfig config_;
    std::string machine_id_;
    storage::SqliteStore& store_;
    std::unique_ptr<GpuSensorReader> reader_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace host_agent::collectors

#endif  // HOST_AGENT_COLLECTORS_DIRECT_GPU_COLLECTOR_H
