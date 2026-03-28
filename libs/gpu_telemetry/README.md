# gpu_telemetry

Reusable NVIDIA GPU telemetry library for Windows. It wraps NVAPI and NVML behind
small APIs centered on `GpuProbe`, `GpuSensorReader`, `GpuSnapshot`, and a
plain C ABI for DLL/FFI callers.

## Public API

Installed consumers should treat only these headers as stable:

- `gpu_telemetry/gpu_telemetry_c.h`
- `gpu_telemetry/gpu_sensor_reader.h`
- `gpu_telemetry/gpu_probe.h`
- `gpu_telemetry/gpu_snapshot.h`

`GpuProbe` exposes shareable discovery metadata through `GpuInfo` and keeps
vendor-specific handles and loader state private to the implementation.
`GpuSensorReader` adds a simpler app-facing facade for one-shot GPU sampling.
`gpu_telemetry_c.h` exposes a stable C ABI with POD structs so the DLL can be
loaded from Python, C, Rust FFI, or other non-C++ callers.

Other headers in this repository support the implementation or low-level
research work, but they are not part of the installed package contract and are
not installed by `cmake --install`.

## Quick Sensor Reader Example

```cpp
#include <gpu_telemetry/gpu_sensor_reader.h>

#include <cstdio>
#include <string>

int main() {
    GpuSensorReader reader;
    std::string warning;
    if (!reader.init(warning)) {
        std::fprintf(stderr, "gpu init failed: %s\n", warning.c_str());
        return 1;
    }

    auto gpus = reader.gpu_info();
    auto samples = reader.sample_all(GpuSampleMode::Full);

    for (size_t i = 0; i < samples.size(); ++i) {
        std::printf("GPU %d %s core=%.1fC fan=%urpm power=%.1fW\n",
                    samples[i].gpu_index,
                    gpus[i].name.c_str(),
                    samples[i].core_c,
                    samples[i].fan_count > 0 ? samples[i].fans[0].rpm : 0u,
                    samples[i].nvml_power_mw / 1000.0);
    }

    reader.shutdown();
    return 0;
}
```

`GpuSampleMode::Full` combines the existing fast and slow probe paths into one
`GpuSnapshot`, which is the most useful mode for a motherboard or system-sensor
application. The other modes expose the lower-latency probe tiers directly when
you want tighter polling or lower overhead.

## Polling Model

This library does not start a background thread or own a polling cadence. The
caller decides when to call `sample()` or `sample_all()` and how to schedule
fast vs. slow tiers.

Each `GpuProbe` sample method returns `bool`: `true` when fresh data was read
from the driver, `false` when the call was skipped because the minimum polling
interval for that tier had not yet elapsed. The built-in rate limits are:

| Tier | Method | Min interval | Max rate |
|------|--------|-------------|----------|
| Thermal fast | `sample_thermal_fast` | 5 ms | ~200 Hz |
| Fast | `sample_fast` | 16 ms | ~60 Hz |
| Medium | `sample_medium` | 16 ms | ~60 Hz |
| Slow | `sample_slow` | 100 ms | ~10 Hz |
| Rare | `sample_rare` | 500 ms | ~2 Hz |

When rate-limited, `GpuProbe` leaves the caller-provided snapshot unchanged.
`GpuSensorReader::sample()` propagates the `bool` return from the probe and
clears the output snapshot before each attempt. `sample_all()` always returns a
vector, but each per-GPU entry starts zeroed, so rate-limited entries are
mostly empty snapshots rather than cached old values.

`GpuSnapshot::time_ms` and `GpuSnapshot::dt_ms` are caller-owned metadata.
The library fills the sensor fields in each snapshot, while the caller can
stamp the sample with its own timing origin and delta if those values are
useful to the application.

## Build

```powershell
cmake --preset x64-release
cmake --build --preset x64-release
```

The library dynamically loads `nvapi64.dll` and `nvml.dll` at runtime. It does
not require the NVIDIA SDK at build time. The release build now emits both:

- `gpu_telemetry.lib`: C++ static library
- `gpu_telemetry_c.dll`: C ABI DLL for FFI callers
- `gpu_telemetry_c.lib`: import library for the C ABI DLL

The packaged release also bundles the required MSVC runtime DLLs under `bin/`.
The exact runtime dependency list and hashes are recorded in
`build-info.json` and `PACKAGE_CONTENTS.json`.

## Release Packaging

Use the release script when you want a shareable package instead of only a local
build tree:

```powershell
.\scripts\build-release.ps1 -NoVersionBump
```

The script uses the `VERSION` file as the package version, builds the Release
targets, installs them with CMake into a staging tree, then produces a versioned
`dist/` package folder and zip containing the DLL, import/static libraries,
public headers, bundled MSVC runtime DLLs, CMake package files, and release
metadata. It also smoke-tests the packaged install with one minimal C++ consumer
and one minimal C consumer before writing the final zip.

## Install As CMake Package

```powershell
cmake --preset x64-release
cmake --build --preset x64-release
cmake --install build/x64-release --prefix D:/dev/packages/gpu_telemetry
```

Downstream projects can then consume it with:

```cmake
find_package(gpu_telemetry CONFIG REQUIRED)
target_link_libraries(my_cpp_app PRIVATE gpu_telemetry::gpu_telemetry)
target_link_libraries(my_c_or_ffi_host PRIVATE gpu_telemetry::gpu_telemetry_c)
```

For `gpu_telemetry::gpu_telemetry_c`, ship `bin/gpu_telemetry_c.dll` alongside
your executable or ensure the package `bin/` directory is on `PATH`.

## C ABI / Python FFI

Use `gpu_telemetry/gpu_telemetry_c.h` together with `gpu_telemetry_c.dll` when
you need a stable ABI instead of the C++ classes.

The C ABI exposes:

- `gpu_telemetry_get_api_version` -- runtime version check (compare against
  `GPU_TELEMETRY_CAPI_VERSION` to detect header/DLL mismatches)
- an opaque `gpu_telemetry_reader_t` handle
- `gpu_telemetry_reader_create/destroy/init/shutdown`
- `gpu_telemetry_reader_get_gpu_count`
- `gpu_telemetry_reader_get_gpu_info`
- `gpu_telemetry_reader_sample`

`gpu_telemetry_reader_sample()` returns `1` only when it produced a fresh
sample. It returns `0` for invalid inputs, uninitialized readers, unsupported
sample modes, and rate-limited calls.

Minimal Python `ctypes` usage:

```python
import ctypes as ct

gpu = ct.WinDLL("gpu_telemetry_c.dll")
reader = gpu.gpu_telemetry_reader_create()
```

## Notes

- `GpuSnapshot` uses `0`, `false`, or `-1` sentinel values when a metric is
  unavailable.
- NVML-backed fields remain optional at runtime. A probe can still initialize
  when `nvml.dll` is missing.
- Internal headers in this repository support the implementation, but they are
  not part of the installed package contract.
