#include "host_agent/api/http_server.h"
#include "host_agent/api/json_response.h"

#include <httplib.h>

#include <chrono>
#include <iostream>

namespace host_agent::api {

namespace {

std::int64_t NowUtcMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

HttpServer::HttpServer(const model::ApiConfig& config,
                       const std::string& machine_id,
                       storage::SqliteStore& store)
    : config_(config),
      machine_id_(machine_id),
      store_(store),
      svr_(std::make_unique<httplib::Server>()) {}

HttpServer::~HttpServer() {
    Stop();
}

void HttpServer::Start() {
    if (running_.load()) return;

    RegisterRoutes();

    running_.store(true);
    thread_ = std::thread([this]() { Run(); });

    std::cout << "HTTP API starting on " << config_.bind << ":" << config_.port << "\n";
}

void HttpServer::Stop() {
    if (!running_.load()) return;

    svr_->stop();
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
}

bool HttpServer::IsRunning() const {
    return running_.load();
}

void HttpServer::RegisterRoutes() {
    // -- /v1/status --
    svr_->Get("/v1/status", [this](const httplib::Request&, httplib::Response& res) {
        auto root = MakeObject();
        AddString(root.get(), "machine_id", machine_id_);
        AddInt64(root.get(), "generated_utc_ms", NowUtcMs());
        AddString(root.get(), "api_version", "1.0");

        auto collectors_json = MakeObject();

        auto collectors = store_.QueryCollectorHealth(machine_id_);
        for (const auto& c : collectors) {
            auto entry = MakeObject();
            AddString(entry.get(), "status", c.status);
            AddOptionalInt64(entry.get(), "last_hello_utc_ms", c.last_hello_utc_ms);
            AddOptionalInt64(entry.get(), "last_sample_utc_ms", c.last_sample_utc_ms);
            AddOptionalString(entry.get(), "last_error", c.last_error);
            AddInt64(entry.get(), "updated_utc_ms", c.updated_utc_ms);
            cJSON_AddItemToObject(collectors_json.get(), c.source.c_str(), entry.release());
        }

        cJSON_AddItemToObject(root.get(), "collectors", collectors_json.release());

        auto machine = store_.QueryMachineCatalog(machine_id_);
        if (machine.has_value()) {
            AddString(root.get(), "hostname", machine->hostname);
            AddString(root.get(), "display_name", machine->display_name);
            AddInt64(root.get(), "first_seen_utc_ms", machine->first_seen_utc_ms);
            AddInt64(root.get(), "last_seen_utc_ms", machine->last_seen_utc_ms);
        }

        res.set_content(PrintJson(root.get()), "application/json");
    });

    // -- /v1/catalog --
    svr_->Get("/v1/catalog", [this](const httplib::Request& req, httplib::Response& res) {
        storage::CatalogFilter filter;
        if (req.has_param("hardware_type")) {
            filter.hardware_type = req.get_param_value("hardware_type");
        }
        if (req.has_param("sensor_type")) {
            filter.sensor_type = req.get_param_value("sensor_type");
        }

        auto sensors = store_.QuerySensorCatalog(machine_id_, filter);

        auto root = MakeObject();
        AddString(root.get(), "machine_id", machine_id_);
        AddInt(root.get(), "sensor_count", static_cast<int>(sensors.size()));

        auto arr = MakeArray();
        for (const auto& s : sensors) {
            auto item = MakeObject();
            AddString(item.get(), "sensor_uid", s.sensor_uid);
            AddString(item.get(), "hardware_name", s.hardware_name);
            AddString(item.get(), "hardware_type", s.hardware_type);
            AddString(item.get(), "sensor_name", s.sensor_name);
            AddString(item.get(), "sensor_type", s.sensor_type);
            AddString(item.get(), "unit", s.unit);
            AddInt(item.get(), "sensor_index", s.sensor_index);
            AddBool(item.get(), "is_default_hidden", s.is_default_hidden);
            cJSON_AddItemToArray(arr.get(), item.release());
        }
        cJSON_AddItemToObject(root.get(), "sensors", arr.release());

        res.set_content(PrintJson(root.get()), "application/json");
    });

    // -- /v1/history --
    svr_->Get("/v1/history", [this](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("sensor_uid")) {
            res.status = 400;
            res.set_content(R"({"error":"sensor_uid parameter is required"})", "application/json");
            return;
        }

        const std::string sensor_uid = req.get_param_value("sensor_uid");

        std::int64_t from_ms = 0;
        std::int64_t to_ms = NowUtcMs();
        int limit = 500;

        if (req.has_param("from")) {
            from_ms = std::stoll(req.get_param_value("from"));
        }
        if (req.has_param("to")) {
            to_ms = std::stoll(req.get_param_value("to"));
        }
        if (req.has_param("limit")) {
            limit = std::stoi(req.get_param_value("limit"));
            if (limit <= 0) limit = 500;
            if (limit > 10000) limit = 10000;
        }

        auto samples = store_.QuerySensorHistory(sensor_uid, from_ms, to_ms, limit);

        auto root = MakeObject();
        AddString(root.get(), "sensor_uid", sensor_uid);
        AddInt64(root.get(), "from_utc_ms", from_ms);
        AddInt64(root.get(), "to_utc_ms", to_ms);
        AddInt(root.get(), "count", static_cast<int>(samples.size()));

        auto arr = MakeArray();
        for (const auto& s : samples) {
            auto item = MakeObject();
            AddInt64(item.get(), "ts_utc_ms", s.ts_utc_ms);
            AddDouble(item.get(), "value", s.value);
            AddOptionalDouble(item.get(), "min", s.min_value);
            AddOptionalDouble(item.get(), "max", s.max_value);
            AddString(item.get(), "quality", s.quality);
            cJSON_AddItemToArray(arr.get(), item.release());
        }
        cJSON_AddItemToObject(root.get(), "samples", arr.release());

        res.set_content(PrintJson(root.get()), "application/json");
    });

    // -- /v1/snapshot --
    svr_->Get("/v1/snapshot", [this](const httplib::Request&, httplib::Response& res) {
        auto snapshot = store_.QueryLatestSnapshot(machine_id_);

        auto root = MakeObject();
        AddString(root.get(), "machine_id", machine_id_);
        AddInt64(root.get(), "generated_utc_ms", NowUtcMs());

        // System section from latest sensor samples
        auto system_json = MakeObject();
        AddOptionalDouble(system_json.get(), "cpu_package_c", snapshot.cpu_package_c);
        AddOptionalDouble(system_json.get(), "cpu_total_load_pct", snapshot.cpu_total_load_pct);
        AddOptionalInt64(system_json.get(), "memory_used_bytes", snapshot.memory_used_bytes);
        AddOptionalInt64(system_json.get(), "memory_total_bytes", snapshot.memory_total_bytes);
        cJSON_AddItemToObject(root.get(), "system", system_json.release());

        // GPU section
        auto gpus_arr = MakeArray();
        for (const auto& gpu : snapshot.gpus) {
            auto gpu_json = MakeObject();
            AddInt(gpu_json.get(), "gpu_index", gpu.gpu_index);
            AddString(gpu_json.get(), "gpu_name", gpu.gpu_name);
            AddOptionalDouble(gpu_json.get(), "core_c", gpu.core_c);
            AddOptionalDouble(gpu_json.get(), "hotspot_c", gpu.hotspot_c);
            AddOptionalDouble(gpu_json.get(), "util_gpu_pct", gpu.util_gpu_pct);
            AddOptionalInt64(gpu_json.get(), "vram_used_bytes", gpu.vram_used_bytes);
            AddOptionalInt64(gpu_json.get(), "vram_total_bytes", gpu.vram_total_bytes);
            AddOptionalDouble(gpu_json.get(), "power_w", gpu.power_w);
            cJSON_AddItemToArray(gpus_arr.get(), gpu_json.release());
        }
        cJSON_AddItemToObject(root.get(), "gpus", gpus_arr.release());

        // Ollama section
        auto ollama_json = MakeObject();
        AddBool(ollama_json.get(), "reachable", snapshot.ollama_reachable);
        AddInt(ollama_json.get(), "loaded_model_count", snapshot.ollama_loaded_model_count);
        AddOptionalInt64(ollama_json.get(), "resident_vram_bytes", snapshot.ollama_resident_vram_bytes);
        cJSON_AddItemToObject(root.get(), "ollama", ollama_json.release());

        // Collectors section
        auto collectors_json = MakeObject();
        auto collectors = store_.QueryCollectorHealth(machine_id_);
        for (const auto& c : collectors) {
            auto entry = MakeObject();
            AddString(entry.get(), "status", c.status);
            std::int64_t age_ms = -1;
            if (c.last_sample_utc_ms.has_value()) {
                age_ms = NowUtcMs() - *c.last_sample_utc_ms;
            }
            AddInt64(entry.get(), "age_ms", age_ms);
            cJSON_AddItemToObject(collectors_json.get(), c.source.c_str(), entry.release());
        }
        cJSON_AddItemToObject(root.get(), "collectors", collectors_json.release());

        res.set_content(PrintJson(root.get()), "application/json");
    });

    // -- /v1/readiness --
    svr_->Get("/v1/readiness", [this](const httplib::Request& req, httplib::Response& res) {
        std::int64_t required_vram_bytes = 0;
        if (req.has_param("required_vram_bytes")) {
            required_vram_bytes = std::stoll(req.get_param_value("required_vram_bytes"));
        }

        auto snapshot = store_.QueryLatestSnapshot(machine_id_);
        auto collectors = store_.QueryCollectorHealth(machine_id_);

        // Compute staleness from GPU collector
        constexpr std::int64_t kStaleThresholdMs = 30000;
        bool gpu_stale = true;
        for (const auto& c : collectors) {
            if (c.source == "direct_gpu" && c.last_sample_utc_ms.has_value()) {
                gpu_stale = (NowUtcMs() - *c.last_sample_utc_ms) > kStaleThresholdMs;
                break;
            }
        }

        // Compute verdict
        std::string verdict;
        std::vector<std::string> reasons;

        if (snapshot.gpus.empty()) {
            // No GPU collector data yet — check if we have LHM GPU sensors
            bool has_any_gpu = false;
            for (const auto& c : collectors) {
                if (c.source == "librehw_bridge" && c.last_sample_utc_ms.has_value()) {
                    has_any_gpu = true;
                    gpu_stale = (NowUtcMs() - *c.last_sample_utc_ms) > kStaleThresholdMs;
                    break;
                }
            }

            if (!has_any_gpu || gpu_stale) {
                verdict = "stale";
                reasons.push_back("no recent sensor data");
            } else {
                verdict = "unknown";
                reasons.push_back("no GPU collector data available");
            }
        } else if (gpu_stale) {
            verdict = "stale";
            reasons.push_back("GPU data is stale");
        } else {
            // Find max utilization and VRAM across GPUs
            double max_util = 0.0;
            std::int64_t total_vram_free = 0;
            bool has_vram = false;

            for (const auto& gpu : snapshot.gpus) {
                if (gpu.util_gpu_pct.has_value()) {
                    max_util = std::max(max_util, *gpu.util_gpu_pct);
                }
                if (gpu.vram_total_bytes.has_value() && gpu.vram_used_bytes.has_value()) {
                    has_vram = true;
                    total_vram_free += (*gpu.vram_total_bytes - *gpu.vram_used_bytes);
                }
            }

            if (!has_vram) {
                verdict = "unknown";
                reasons.push_back("no VRAM data available");
            } else if (required_vram_bytes > 0 && total_vram_free < required_vram_bytes) {
                verdict = "insufficient_vram";
                reasons.push_back("required " + std::to_string(required_vram_bytes) +
                                  " bytes but only " + std::to_string(total_vram_free) + " free");
            } else if (max_util < 30.0) {
                verdict = "fits_idle";
            } else if (max_util < 80.0) {
                verdict = "fits_available";
            } else {
                verdict = "fits_busy";
                reasons.push_back("GPU utilization at " + std::to_string(static_cast<int>(max_util)) + "%");
            }
        }

        auto root = MakeObject();
        AddString(root.get(), "machine_id", machine_id_);
        AddString(root.get(), "verdict", verdict);
        AddInt64(root.get(), "generated_utc_ms", NowUtcMs());

        // GPU summary
        auto gpu_summary = MakeObject();
        AddInt(gpu_summary.get(), "gpu_count", static_cast<int>(snapshot.gpus.size()));
        if (!snapshot.gpus.empty()) {
            double max_util = 0.0, max_temp = 0.0, total_power = 0.0;
            std::int64_t vram_free = 0, vram_total = 0;
            for (const auto& gpu : snapshot.gpus) {
                if (gpu.util_gpu_pct.has_value()) max_util = std::max(max_util, *gpu.util_gpu_pct);
                if (gpu.core_c.has_value()) max_temp = std::max(max_temp, *gpu.core_c);
                if (gpu.power_w.has_value()) total_power += *gpu.power_w;
                if (gpu.vram_total_bytes.has_value()) vram_total += *gpu.vram_total_bytes;
                if (gpu.vram_used_bytes.has_value() && gpu.vram_total_bytes.has_value())
                    vram_free += (*gpu.vram_total_bytes - *gpu.vram_used_bytes);
            }
            AddDouble(gpu_summary.get(), "max_util_pct", max_util);
            AddDouble(gpu_summary.get(), "max_temperature_c", max_temp);
            AddDouble(gpu_summary.get(), "total_power_w", total_power);
            AddInt64(gpu_summary.get(), "vram_free_bytes", vram_free);
            AddInt64(gpu_summary.get(), "vram_total_bytes", vram_total);
        }
        cJSON_AddItemToObject(root.get(), "gpu_summary", gpu_summary.release());

        // Ollama section
        auto ollama_json = MakeObject();
        AddBool(ollama_json.get(), "reachable", snapshot.ollama_reachable);
        AddInt(ollama_json.get(), "loaded_model_count", snapshot.ollama_loaded_model_count);
        AddOptionalInt64(ollama_json.get(), "resident_vram_bytes", snapshot.ollama_resident_vram_bytes);
        cJSON_AddItemToObject(root.get(), "ollama", ollama_json.release());

        // Reasons array
        auto reasons_arr = MakeArray();
        for (const auto& r : reasons) {
            cJSON_AddItemToArray(reasons_arr.get(), cJSON_CreateString(r.c_str()));
        }
        cJSON_AddItemToObject(root.get(), "reasons", reasons_arr.release());

        res.set_content(PrintJson(root.get()), "application/json");
    });
}

void HttpServer::Run() {
    if (!svr_->listen(config_.bind, config_.port)) {
        std::cerr << "HTTP server failed to listen on "
                  << config_.bind << ":" << config_.port << "\n";
        running_.store(false);
    }
}

}  // namespace host_agent::api
