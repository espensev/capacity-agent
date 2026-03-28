#include "nvapi_thermals.h"

#include <cstring>

bool NvApiThermals::init(const NvApiLoader& loader, std::string& out_warning) {
    if (!loader.is_ready()) {
        out_warning = "NVAPI loader is not ready.";
        return false;
    }

    fn_get_thermal_u_ = loader.resolve<NvAPI_GPU_GetThermalSensorsUndoc_t>(
        NVAPI_ID_GPU_GET_THERMAL_SENSORS_UNDOC);

    if (!fn_get_thermal_u_) {
        out_warning = "Cannot resolve undocumented thermal sensor function (0x65FE3AAD).";
        shutdown();
        return false;
    }

    ready_ = true;
    return true;
}

void NvApiThermals::shutdown() {
    ready_ = false;
    fn_get_thermal_u_ = nullptr;
}

bool NvApiThermals::discover_sensors(NvPhysicalGpuHandle gpu, NvThermalDiscovery& out) const {
    if (!ready_ || !fn_get_thermal_u_) {
        return false;
    }

    out = {};

    /* Phase 1: Per-bit probe to find valid sensor indices.
     * Version 2 struct with mask = single bit.  Probe all 32 bits
     * to handle sparse sensor layouts (gaps between valid indices). */
    int max_bit = 0;
    for (int bit = 0; bit < 32; bit++) {
        NvUndocThermalSensors probe;
        std::memset(&probe, 0, sizeof(probe));
        probe.version = nvapi_make_version<NvUndocThermalSensors>(2);
        probe.mask = 1u << bit;
        if (fn_get_thermal_u_(gpu, &probe) == NVAPI_OK) {
            out.mask |= (1u << bit);
            max_bit = bit + 1;
        }
    }

    if (out.mask == 0) {
        return false;
    }

    out.count = max_bit;

    /* Phase 2: Full read with discovered mask to get initial values */
    NvUndocThermalSensors us;
    std::memset(&us, 0, sizeof(us));
    us.version = nvapi_make_version<NvUndocThermalSensors>(2);
    us.mask = out.mask;

    if (fn_get_thermal_u_(gpu, &us) != NVAPI_OK) {
        out = {};
        return false;
    }

    std::memcpy(out.initial, us.temperatures, sizeof(out.initial));

    /* Phase 3: Identify sensor roles */

    /* Core temperature is always at index 1 (if it exists and responds) */
    if (max_bit > 1 && (out.mask & (1u << 1))) {
        out.core_index = 1;
    }

    /* Memory junction: generation-dependent index.
     * RTX 50xx: idx 2, RTX 40xx: idx 7, RTX 30xx: idx 9 or 8.
     * Pick first non-zero, non-sentinel candidate. */
    static const int mem_candidates[] = { 2, 7, 9, 8 };
    for (int idx : mem_candidates) {
        if (idx < max_bit
            && (out.mask & (1u << idx))
            && us.temperatures[idx] != 0
            && us.temperatures[idx] != SENTINEL_RAW) {
            out.memj_index = idx;
            break;
        }
    }

    return true;
}

bool NvApiThermals::read(NvPhysicalGpuHandle gpu, uint32_t mask,
                          int32_t out[NVAPI_UNDOC_THERMAL_VALUES]) const {
    if (!ready_ || !fn_get_thermal_u_ || mask == 0) {
        return false;
    }

    NvUndocThermalSensors us;
    std::memset(&us, 0, sizeof(us));
    us.version = nvapi_make_version<NvUndocThermalSensors>(2);
    us.mask = mask;

    if (fn_get_thermal_u_(gpu, &us) != NVAPI_OK) {
        return false;
    }

    std::memcpy(out, us.temperatures, sizeof(us.temperatures));
    return true;
}
