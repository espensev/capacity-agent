using System.Text.Json.Serialization;

namespace LibreHwBridge;

public sealed record BridgeOptions(
    string PipeName,
    string MachineId,
    int SampleIntervalMs,
    int ReconnectDelayMs,
    string Source,
    string BridgeVersion);

public sealed record HelloMessage(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("schema_version")] int SchemaVersion,
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("machine_id")] string MachineId,
    [property: JsonPropertyName("sent_at_utc")] string SentAtUtc,
    [property: JsonPropertyName("bridge_version")] string BridgeVersion,
    [property: JsonPropertyName("hostname")] string Hostname,
    [property: JsonPropertyName("pid")] int Pid,
    [property: JsonPropertyName("sample_interval_ms")] int SampleIntervalMs,
    [property: JsonPropertyName("capabilities")] BridgeCapabilities Capabilities);

public sealed record BridgeCapabilities(
    [property: JsonPropertyName("hardware_types")] IReadOnlyList<string> HardwareTypes,
    [property: JsonPropertyName("sensor_types")] IReadOnlyList<string> SensorTypes);

public sealed record CatalogSnapshotMessage(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("schema_version")] int SchemaVersion,
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("machine_id")] string MachineId,
    [property: JsonPropertyName("sent_at_utc")] string SentAtUtc,
    [property: JsonPropertyName("sensors")] IReadOnlyList<CatalogSensor> Sensors);

public sealed record CatalogSensor(
    [property: JsonPropertyName("sensor_uid")] string SensorUid,
    [property: JsonPropertyName("hardware_uid")] string HardwareUid,
    [property: JsonPropertyName("hardware_path")] string HardwarePath,
    [property: JsonPropertyName("hardware_name")] string HardwareName,
    [property: JsonPropertyName("hardware_type")] string HardwareType,
    [property: JsonPropertyName("sensor_path")] string SensorPath,
    [property: JsonPropertyName("sensor_name")] string SensorName,
    [property: JsonPropertyName("sensor_type")] string SensorType,
    [property: JsonPropertyName("sensor_index")] int SensorIndex,
    [property: JsonPropertyName("unit")] string Unit,
    [property: JsonPropertyName("is_default_hidden")] bool IsDefaultHidden,
    [property: JsonPropertyName("properties")] IReadOnlyDictionary<string, string> Properties);

public sealed record SampleBatchMessage(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("schema_version")] int SchemaVersion,
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("machine_id")] string MachineId,
    [property: JsonPropertyName("sent_at_utc")] string SentAtUtc,
    [property: JsonPropertyName("sample_time_utc")] string SampleTimeUtc,
    [property: JsonPropertyName("samples")] IReadOnlyList<SensorSample> Samples);

public sealed record SensorSample(
    [property: JsonPropertyName("sensor_uid")] string SensorUid,
    [property: JsonPropertyName("value")] float Value,
    [property: JsonPropertyName("min")] float? Min,
    [property: JsonPropertyName("max")] float? Max,
    [property: JsonPropertyName("quality")] string Quality);

public sealed record HeartbeatMessage(
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("schema_version")] int SchemaVersion,
    [property: JsonPropertyName("source")] string Source,
    [property: JsonPropertyName("machine_id")] string MachineId,
    [property: JsonPropertyName("sent_at_utc")] string SentAtUtc,
    [property: JsonPropertyName("status")] string Status,
    [property: JsonPropertyName("uptime_s")] long UptimeSeconds,
    [property: JsonPropertyName("catalog_count")] int CatalogCount);

