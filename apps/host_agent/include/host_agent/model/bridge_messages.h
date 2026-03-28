#ifndef HOST_AGENT_MODEL_BRIDGE_MESSAGES_H
#define HOST_AGENT_MODEL_BRIDGE_MESSAGES_H

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace host_agent::model {

struct MessageEnvelope {
    int schema_version = 0;
    std::string source;
    std::string machine_id;
    std::int64_t sent_at_utc_ms = 0;
};

struct HelloMessage {
    MessageEnvelope envelope;
    std::string bridge_version;
    std::string hostname;
    int pid = 0;
    int sample_interval_ms = 0;
    std::string capabilities_json;
};

struct CatalogSensor {
    std::string sensor_uid;
    std::string hardware_uid;
    std::string hardware_path;
    std::string hardware_name;
    std::string hardware_type;
    std::string sensor_path;
    std::string sensor_name;
    std::string sensor_type;
    int sensor_index = 0;
    std::string unit;
    bool is_default_hidden = false;
    std::string properties_json;
};

struct CatalogSnapshotMessage {
    MessageEnvelope envelope;
    std::vector<CatalogSensor> sensors;
};

struct SensorSample {
    std::string sensor_uid;
    double value = 0.0;
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::string quality;
};

struct SampleBatchMessage {
    MessageEnvelope envelope;
    std::int64_t sample_time_utc_ms = 0;
    std::vector<SensorSample> samples;
};

struct HeartbeatMessage {
    MessageEnvelope envelope;
    std::string status;
    std::int64_t uptime_seconds = 0;
    int catalog_count = 0;
};

using BridgeMessage = std::variant<
    HelloMessage,
    CatalogSnapshotMessage,
    SampleBatchMessage,
    HeartbeatMessage>;

}  // namespace host_agent::model

#endif  // HOST_AGENT_MODEL_BRIDGE_MESSAGES_H

