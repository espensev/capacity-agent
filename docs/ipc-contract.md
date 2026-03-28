# Named Pipe Contract

## Transport

- Windows named pipe
- default pipe name: `\\.\pipe\ollama_host_agent_lhm`
- UTF-8 encoded NDJSON
- one complete JSON object per line
- timestamps encoded as RFC 3339 / ISO 8601 UTC strings

`host_agent` is the named-pipe server.

`librehw_bridge` is the client and reconnects on failure.

## Envelope

Each message has:

```json
{
  "type": "hello",
  "schema_version": 1,
  "source": "librehw_bridge",
  "machine_id": "snd-host",
  "sent_at_utc": "2026-03-28T11:02:00.0000000Z"
}
```

Common fields:

- `type`
- `schema_version`
- `source`
- `machine_id`
- `sent_at_utc`

## Message Types

### `hello`

Sent once after connecting.

```json
{
  "type": "hello",
  "schema_version": 1,
  "source": "librehw_bridge",
  "machine_id": "snd-host",
  "sent_at_utc": "2026-03-28T11:02:00.0000000Z",
  "bridge_version": "0.1.0",
  "hostname": "SND-HOST",
  "pid": 14220,
  "sample_interval_ms": 1000,
  "capabilities": {
    "hardware_types": ["Cpu", "GpuNvidia", "Memory", "Motherboard", "Controller", "Network", "Storage"],
    "sensor_types": ["Temperature", "Load", "Clock", "Fan", "Power", "Voltage", "Current", "Data", "Throughput", "Flow", "Humidity", "TemperatureRate"]
  }
}
```

### `catalog_snapshot`

Sent after `hello` and again whenever the bridge decides the catalog changed.

```json
{
  "type": "catalog_snapshot",
  "schema_version": 1,
  "source": "librehw",
  "machine_id": "snd-host",
  "sent_at_utc": "2026-03-28T11:02:00.1000000Z",
  "sensors": [
    {
      "sensor_uid": "librehw:/amdcpu/0/temperature/0",
      "hardware_uid": "librehw:/amdcpu/0",
      "hardware_path": "/amdcpu/0",
      "hardware_name": "AMD Ryzen 7 7800X3D",
      "hardware_type": "Cpu",
      "sensor_path": "/amdcpu/0/temperature/0",
      "sensor_name": "CPU Package",
      "sensor_type": "Temperature",
      "sensor_index": 0,
      "unit": "C",
      "is_default_hidden": false,
      "properties": {
        "Parent": "none"
      }
    }
  ]
}
```

Field rules:

- `sensor_uid` is the durable primary key for samples
- `sensor_uid = "librehw:" + sensor.Identifier`
- `hardware_uid = "librehw:" + hardware.Identifier`
- `unit` is derived from `SensorType`

### `sample_batch`

Sent at the configured collection interval.

```json
{
  "type": "sample_batch",
  "schema_version": 1,
  "source": "librehw",
  "machine_id": "snd-host",
  "sent_at_utc": "2026-03-28T11:02:01.0000000Z",
  "sample_time_utc": "2026-03-28T11:02:01.0000000Z",
  "samples": [
    {
      "sensor_uid": "librehw:/amdcpu/0/temperature/0",
      "value": 62.5,
      "min": 58.0,
      "max": 71.0,
      "quality": "ok"
    }
  ]
}
```

Rules:

- omit null sensor values instead of sending `"value": null`
- keep `min` and `max` when the source exposes them
- `quality` values currently expected: `ok`, `stale`, `unsupported`

### `heartbeat`

Optional periodic health message.

```json
{
  "type": "heartbeat",
  "schema_version": 1,
  "source": "librehw_bridge",
  "machine_id": "snd-host",
  "sent_at_utc": "2026-03-28T11:03:00.0000000Z",
  "status": "ok",
  "uptime_s": 60,
  "catalog_count": 412
}
```

## Unit Mapping

Suggested unit map:

- `Temperature` -> `C`
- `TemperatureRate` -> `C_per_s`
- `Load` -> `pct`
- `Clock` -> `MHz`
- `Voltage` -> `V`
- `Current` -> `A`
- `Power` -> `W`
- `Fan` -> `rpm`
- `Control` -> `pct`
- `Flow` -> `L_per_h`
- `Data` -> `GiB`
- `SmallData` -> `MiB`
- `Throughput` -> `B_per_s`
- `TimeSpan` -> `s`
- `Energy` -> `mWh`
- `Humidity` -> `pct`

## Host Behavior

On receipt:

1. upsert bridge health from `hello`
2. upsert `sensor_catalog` from `catalog_snapshot`
3. insert `sensor_samples` from `sample_batch`
4. update `collector_health.last_sample_utc_ms`

