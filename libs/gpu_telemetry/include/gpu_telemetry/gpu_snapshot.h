#ifndef GPU_SNAPSHOT_H
#define GPU_SNAPSHOT_H

#include <cstdint>

/*
 * Unified GPU telemetry record.  One struct per sample.
 * Fields set to 0 / false when data is unavailable.
 */

constexpr int GPU_SNAPSHOT_MAX_FANS       = 4;
constexpr int GPU_SNAPSHOT_MAX_POWER_RAILS = 4;

struct GpuSnapshot {
    /* ---- Metadata ---- */
    int      gpu_index      = 0;
    int64_t  time_ms        = 0;    /* elapsed since origin */
    int64_t  dt_ms          = 0;    /* delta from previous sample */

    /* ---- Thermals (undoc NVAPI, Q8.8 -> Celsius) ---- */
    double   core_c         = 0.0;
    double   memjn_c        = 0.0;
    /* ---- Thermals (documented NVAPI) ---- */
    // Hotspot sensor: may not exist on all GPUs. On RTX 5090, this sensor
    // reports values but they track close to core temp — not a true hotspot
    // delta. Treat as informational/cross-validation only.
    int32_t  hotspot_c      = 0;    /* 0 = unavailable */
    /* ---- Thermals (NVML cross-validation) ---- */
    unsigned int nvml_temp_c = 0;

    /* ---- Clocks (documented NVAPI, MHz) ---- */
    unsigned int clock_graphics_mhz = 0;
    unsigned int clock_memory_mhz   = 0;
    unsigned int clock_video_mhz    = 0;
    unsigned int clock_boost_mhz    = 0;

    /* ---- Clocks (NVML, MHz) ---- */
    unsigned int nvml_clock_graphics_mhz = 0;
    unsigned int nvml_clock_memory_mhz   = 0;
    unsigned int nvml_clock_video_mhz    = 0;

    /* ---- P-state ---- */
    int32_t  pstate          = -1;  /* 0=P0 .. 15=P15, -1=unknown */

    /* ---- Utilization (NVAPI, documented) ---- */
    int32_t  util_gpu_pct    = -1;  /* -1 = unavailable */
    int32_t  util_fb_pct     = -1;
    int32_t  util_vid_pct    = -1;

    /* ---- Utilization (NVML) ---- */
    unsigned int nvml_util_gpu_pct = 0;
    unsigned int nvml_util_mem_pct = 0;
    unsigned int nvml_encoder_pct  = 0;
    unsigned int nvml_decoder_pct  = 0;

    /* ---- VRAM (NVML, MB) ---- */
    unsigned int vram_used_mb  = 0;
    unsigned int vram_free_mb  = 0;
    unsigned int vram_total_mb = 0;

    /* ---- Fans (undoc NVAPI) ---- */
    int      fan_count = 0;
    struct FanEntry {
        uint32_t level_pct = 0;
        uint32_t rpm       = 0;
    } fans[GPU_SNAPSHOT_MAX_FANS];

    /* ---- Power (NVML board total) ---- */
    unsigned int nvml_power_mw = 0;

    /* ---- Power (undoc NVAPI per-rail) ---- */
    // WARNING: These per-domain values are unreliable on RTX 5090 (~83-92W constant
    // regardless of load). For total board power use nvml_power_mw (matches LHM
    // "GPU Package" / HWiNFO "GPUPower"). Kept for diagnostic/research purposes.
    int      power_rail_count = 0;
    struct PowerRailEntry {
        uint32_t domain  = 0;
        uint32_t power_mw = 0;
    } power_rails[GPU_SNAPSHOT_MAX_POWER_RAILS];

    /* ---- Voltage (documented NVAPI, millivolts) ---- */
    uint32_t voltage_core_mv = 0;

    /* ---- PCIe (NVML) ---- */
    unsigned int pcie_tx_kb_s   = 0;
    unsigned int pcie_rx_kb_s   = 0;
    unsigned int pcie_link_gen  = 0;
    unsigned int pcie_link_width = 0;

    /* ---- Throttle (NVML bitmask) ---- */
    uint64_t throttle_reasons = 0;
};

#endif /* GPU_SNAPSHOT_H */
