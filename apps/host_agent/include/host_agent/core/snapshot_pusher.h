#ifndef HOST_AGENT_CORE_SNAPSHOT_PUSHER_H
#define HOST_AGENT_CORE_SNAPSHOT_PUSHER_H

#include "host_agent/model/telemetry_models.h"

#include <atomic>
#include <string>
#include <thread>

namespace host_agent::core {

/// Periodically GETs the local /v1/snapshot and POSTs it to a remote
/// capacity-api ingest endpoint. Reuses the local HTTP server's output
/// so the pushed payload is always identical to what a pull would return.
class SnapshotPusher {
   public:
    SnapshotPusher(const model::PushConfig& config,
                   const model::ApiConfig& local_api);
    ~SnapshotPusher();

    void Start();
    void Stop();

   private:
    void PushLoop();
    bool PushOnce();

    model::PushConfig config_;
    model::ApiConfig local_api_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace host_agent::core

#endif  // HOST_AGENT_CORE_SNAPSHOT_PUSHER_H
