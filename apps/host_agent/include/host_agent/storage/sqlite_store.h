#ifndef HOST_AGENT_STORAGE_SQLITE_STORE_H
#define HOST_AGENT_STORAGE_SQLITE_STORE_H

#include "host_agent/model/bridge_messages.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace host_agent::storage {

// -- Read query result types --

struct CollectorHealthRow {
    std::string collector_id;
    std::string machine_id;
    std::string source;
    std::string collector_type;
    std::string status;
    std::optional<std::int64_t> last_hello_utc_ms;
    std::optional<std::int64_t> last_sample_utc_ms;
    std::optional<std::string> last_error;
    std::int64_t updated_utc_ms = 0;
};

struct MachineRow {
    std::string machine_id;
    std::string hostname;
    std::string display_name;
    std::int64_t first_seen_utc_ms = 0;
    std::int64_t last_seen_utc_ms = 0;
};

struct CatalogFilter {
    std::optional<std::string> hardware_type;
    std::optional<std::string> sensor_type;
};

struct SensorCatalogRow {
    std::string sensor_uid;
    std::string hardware_name;
    std::string hardware_type;
    std::string sensor_name;
    std::string sensor_type;
    std::string unit;
    int sensor_index = 0;
    bool is_default_hidden = false;
};

struct SensorSampleRow {
    std::int64_t ts_utc_ms = 0;
    double value = 0.0;
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::string quality;
};

struct GpuSnapshotRow {
    int gpu_index = 0;
    std::string gpu_name;
    std::optional<double> core_c;
    std::optional<double> hotspot_c;
    std::optional<double> util_gpu_pct;
    std::optional<std::int64_t> vram_used_bytes;
    std::optional<std::int64_t> vram_total_bytes;
    std::optional<double> power_w;
};

struct LatestSnapshot {
    // System metrics (from latest_sensor_samples or system_samples)
    std::optional<double> cpu_package_c;
    std::optional<double> cpu_total_load_pct;
    std::optional<std::int64_t> memory_used_bytes;
    std::optional<std::int64_t> memory_total_bytes;

    // GPU metrics
    std::vector<GpuSnapshotRow> gpus;

    // Ollama metrics
    bool ollama_reachable = false;
    int ollama_loaded_model_count = 0;
    std::optional<std::int64_t> ollama_resident_vram_bytes;
};

class SqliteStore {
public:
    SqliteStore(std::filesystem::path sqlite_path, std::filesystem::path schema_path);
    ~SqliteStore();

    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

    // Schema
    bool EnsureSchema();

    // Write (pipe ingestion)
    bool UpsertHello(const model::HelloMessage& message);
    bool UpsertCatalogSnapshot(const model::CatalogSnapshotMessage& message);
    bool InsertSampleBatch(const model::SampleBatchMessage& message);
    bool UpsertHeartbeat(const model::HeartbeatMessage& message);

    // Write (collectors)
    bool InsertGpuSample(const std::string& machine_id, const std::string& gpu_uid,
                         int gpu_index, std::int64_t ts_utc_ms,
                         const std::optional<std::string>& gpu_name,
                         const std::string& backend,
                         const std::optional<double>& core_c,
                         const std::optional<double>& hotspot_c,
                         const std::optional<double>& mem_junction_c,
                         const std::optional<double>& util_gpu_pct,
                         const std::optional<double>& util_mem_pct,
                         const std::optional<double>& power_w,
                         const std::optional<std::int64_t>& vram_used_bytes,
                         const std::optional<std::int64_t>& vram_total_bytes,
                         const std::optional<int>& clock_graphics_mhz,
                         const std::optional<int>& clock_memory_mhz,
                         const std::optional<int>& fan0_rpm,
                         const std::optional<double>& fan0_pct);

    bool InsertOllamaRuntimeSample(const std::string& machine_id,
                                   std::int64_t ts_utc_ms,
                                   const std::string& base_url,
                                   const std::optional<int>& active_requests,
                                   const std::optional<int>& queued_requests,
                                   const std::optional<int>& loaded_models,
                                   const std::optional<std::int64_t>& resident_vram_bytes,
                                   const std::optional<std::int64_t>& resident_ram_bytes);

    bool UpsertCollectorHealthDirect(const std::string& machine_id,
                                     const std::string& source,
                                     const std::string& collector_type,
                                     const std::string& status,
                                     std::int64_t now_utc_ms,
                                     const std::optional<std::string>& last_error);

    // Read (API queries)
    std::vector<CollectorHealthRow> QueryCollectorHealth(const std::string& machine_id);
    std::optional<MachineRow> QueryMachineCatalog(const std::string& machine_id);
    std::vector<SensorCatalogRow> QuerySensorCatalog(const std::string& machine_id,
                                                     const CatalogFilter& filter);
    std::vector<SensorSampleRow> QuerySensorHistory(const std::string& sensor_uid,
                                                    std::int64_t from_ms,
                                                    std::int64_t to_ms,
                                                    int limit);
    LatestSnapshot QueryLatestSnapshot(const std::string& machine_id);

private:
    bool OpenIfNeeded();
    void Close();

    std::filesystem::path sqlite_path_;
    std::filesystem::path schema_path_;
    sqlite3* db_ = nullptr;
};

}  // namespace host_agent::storage

#endif  // HOST_AGENT_STORAGE_SQLITE_STORE_H
