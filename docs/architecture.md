# Architecture

## Goal

Run a robust single-machine telemetry host for local Ollama execution and remote orchestration.

Repository ownership follows the runtime split:

- `apps/host_agent` for the native service
- `bridges/librehw_bridge` for managed sensor ingestion
- `libs/gpu_telemetry` for direct NVIDIA telemetry
- `third_party` for vendored dependencies only

The important distinction is:

- `LibreHardwareMonitorLib` provides broad host telemetry.
- direct GPU probing remains separate for control-critical NVIDIA signals.
- the native host owns persistence, policy, and remote-access boundaries.

## Process Layout

```text
+--------------------+      named pipe (NDJSON)      +----------------------+
| librehw_bridge     | ----------------------------> | host_agent           |
| .NET 8             |                               | native C++           |
| loads LHM library  |                               | owns storage/control |
+--------------------+                               +----------------------+
           |                                                    |
           |                                                    +--> SQLite
           |
           +--> LibreHardwareMonitorLib
```

## Why This Split

### Keep C++ as the main runtime

That matches the desired long-term host shape and keeps:

- supervision
- local IPC
- SQLite ownership
- Ollama polling
- GPU-specific logic
- future control actions

inside one native process.

### Keep LibreHardwareMonitor in a managed sidecar

`LibreHardwareMonitorLib` is a managed .NET library. A local bridge avoids:

- C++/CLI build complexity
- CLR hosting inside the main native process
- tighter coupling between host lifecycle and managed dependency failures

The bridge can crash, restart, reconnect, and recatalog without taking down the host.

## Data Sources

### 1. LibreHardwareMonitor bridge

Use for:

- CPU package and core temperatures
- motherboard sensors
- RAM usage sensors exposed by LHM
- storage temperatures and load
- NIC throughput
- generic fan, voltage, current, flow, humidity, and board-specific sensors

### 2. Direct GPU collector

Use for:

- GPU utilization
- VRAM usage
- clocks
- PCIe throughput
- board power
- thermal details you trust on NVIDIA

This remains separate because the current GPU path was primarily validated on RTX 5090 and may need explicit verification on RTX 3080-class cards.

### 3. Ollama collector

Use for:

- active model state
- queue depth
- request duration
- tokens per second
- failures
- load and unload behavior

## Persistence Model

The schema is hybrid:

- `sensor_catalog` and `sensor_samples` preserve generic LHM coverage
- `system_samples` and `gpu_samples` provide stable wide tables for fast control queries
- `ollama_runtime_samples` and `ollama_requests` capture serving behavior
- `control_events` records decisions and outcomes

This avoids losing detail while keeping queries practical.

## Access Model

Recommended boundary:

- `host_agent` binds to `127.0.0.1`
- remote access happens through SSH tunneling or a reverse proxy with TLS and auth

Not recommended:

- exposing the raw collection service directly to the LAN or internet
