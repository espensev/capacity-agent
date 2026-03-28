#include "host_agent/collectors/direct_gpu_collector.h"

#include "gpu_telemetry/gpu_sensor_reader.h"

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

template <typename T>
std::optional<T> Opt(T value, T sentinel) {
    return value != sentinel ? std::optional<T>{value} : std::nullopt;
}

std::optional<double> OptDouble(double value) {
    return value != 0.0 ? std::optional<double>{value} : std::nullopt;
}

std::optional<int> OptInt(int value) {
    return value > 0 ? std::optional<int>{value} : std::nullopt;
}

std::optional<std::int64_t> OptInt64(unsigned int value) {
    return value > 0 ? std::optional<std::int64_t>{static_cast<std::int64_t>(value)} : std::nullopt;
}

}  // namespace

DirectGpuCollector::DirectGpuCollector(const model::GpuCollectorConfig& config,
                                       const std::string& machine_id,
                                       storage::SqliteStore& store)
    : config_(config),
      machine_id_(machine_id),
      store_(store),
      reader_(std::make_unique<GpuSensorReader>()) {}

DirectGpuCollector::~DirectGpuCollector() {
    Stop();
}

bool DirectGpuCollector::Initialize() {
    std::string warning;
    if (!reader_->init(warning)) {
        std::cerr << "GPU collector init failed";
        if (!warning.empty()) {
            std::cerr << ": " << warning;
        }
        std::cerr << "\n";
        store_.UpsertCollectorHealthDirect(
            machine_id_, "direct_gpu", "gpu_poller", "error", NowUtcMs(),
            "init failed: " + warning);
        return false;
    }

    if (!warning.empty()) {
        std::cout << "GPU collector warning: " << warning << "\n";
    }

    std::cout << "GPU collector initialized: " << reader_->gpu_count() << " GPU(s) found\n";
    for (const auto& info : reader_->gpu_info()) {
        std::cout << "  GPU " << info.index << ": " << info.name << "\n";
    }

    return true;
}

void DirectGpuCollector::Start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread([this]() { PollLoop(); });
}

void DirectGpuCollector::Stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    if (reader_->is_initialized()) {
        reader_->shutdown();
    }
}

std::string DirectGpuCollector::Name() const {
    return "direct_gpu_collector";
}

void DirectGpuCollector::PollLoop() {
    while (running_.load()) {
        const auto start = std::chrono::steady_clock::now();

        auto snapshots = reader_->sample_all(GpuSampleMode::Full);
        const std::int64_t now_ms = NowUtcMs();

        for (const auto& snap : snapshots) {
            const auto infos = reader_->gpu_info();
            std::string gpu_name;
            if (snap.gpu_index >= 0 && snap.gpu_index < static_cast<int>(infos.size())) {
                gpu_name = infos[snap.gpu_index].name;
            }

            const std::string gpu_uid = "nvapi:" + std::to_string(snap.gpu_index);

            // Convert VRAM from MB to bytes
            std::optional<std::int64_t> vram_used = OptInt64(snap.vram_used_mb);
            std::optional<std::int64_t> vram_total = OptInt64(snap.vram_total_mb);
            if (vram_used) *vram_used *= 1048576LL;
            if (vram_total) *vram_total *= 1048576LL;

            // Power: use NVML board power (in milliwatts → watts)
            std::optional<double> power_w;
            if (snap.nvml_power_mw > 0) {
                power_w = static_cast<double>(snap.nvml_power_mw) / 1000.0;
            }

            store_.InsertGpuSample(
                machine_id_, gpu_uid, snap.gpu_index, now_ms,
                gpu_name.empty() ? std::nullopt : std::optional<std::string>{gpu_name},
                "nvml_nvapi",
                OptDouble(snap.core_c),
                Opt<std::int32_t>(snap.hotspot_c, 0) ? std::optional<double>{static_cast<double>(snap.hotspot_c)} : std::nullopt,
                OptDouble(snap.memjn_c),
                snap.util_gpu_pct >= 0 ? std::optional<double>{static_cast<double>(snap.util_gpu_pct)} : std::nullopt,
                snap.nvml_util_mem_pct > 0 ? std::optional<double>{static_cast<double>(snap.nvml_util_mem_pct)} : std::nullopt,
                power_w,
                vram_used, vram_total,
                OptInt(static_cast<int>(snap.clock_graphics_mhz)),
                OptInt(static_cast<int>(snap.clock_memory_mhz)),
                snap.fan_count > 0 ? std::optional<int>{static_cast<int>(snap.fans[0].rpm)} : std::nullopt,
                snap.fan_count > 0 ? std::optional<double>{static_cast<double>(snap.fans[0].level_pct)} : std::nullopt);
        }

        // Update collector health
        store_.UpsertCollectorHealthDirect(
            machine_id_, "direct_gpu", "gpu_poller", "ok", now_ms, std::nullopt);

        // Sleep for configured interval minus elapsed time
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto target = std::chrono::milliseconds(config_.sample_interval_ms);
        if (elapsed < target) {
            std::this_thread::sleep_for(target - elapsed);
        }
    }
}

}  // namespace host_agent::collectors
