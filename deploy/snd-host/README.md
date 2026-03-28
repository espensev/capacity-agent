# SND-HOST Deployment

capacity-agent collects local hardware telemetry (GPU, CPU, memory) and exposes it via HTTP API on port 8091.

## Prerequisites
- LibreHardwareMonitor running (for CPU/memory/system sensors via bridge)
- NVIDIA drivers installed (for GPU collector)
- `host_agent.exe` deployed to `D:\Development\capacity-agent\bin\`
- `schema.sql` deployed to `D:\Development\capacity-agent\sql\`

## Quick Start

**Terminal 1** — start the agent:
```powershell
.\start-agent.ps1
```

**Terminal 2** — run tests (while agent is running):
```powershell
.\test-agent.ps1
```

Results are saved to `test-results.txt`. Copy this file back to MAINDESK for review.

## What Gets Tested
| Endpoint | Purpose |
|----------|---------|
| `/v1/status` | Machine info + collector health |
| `/v1/snapshot` | Latest CPU, GPU, memory, Ollama metrics |
| `/v1/readiness` | Can this machine handle a workload? |
| `/v1/catalog` | Available sensor list |
| `/v1/history` | Time-series sensor data |

## Network
The agent binds to `0.0.0.0:8091` so MAINDESK can reach it at `http://192.168.2.5:8091`.
Windows Firewall may need an inbound rule for TCP 8091.

## Push Mode (optional)
To have the agent push snapshots to the central API instead of being polled, add:
```
--push-url http://192.168.2.2:8099/api/ingest/snapshot
```
