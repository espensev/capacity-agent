#include "host_agent/core/bridge_message_parser.h"

#include "cJSON.h"

#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>

namespace host_agent::core {

namespace {

using host_agent::model::BridgeMessage;
using host_agent::model::CatalogSensor;
using host_agent::model::CatalogSnapshotMessage;
using host_agent::model::HeartbeatMessage;
using host_agent::model::HelloMessage;
using host_agent::model::MessageEnvelope;
using host_agent::model::SampleBatchMessage;
using host_agent::model::SensorSample;

bool ParseInt(const std::string& text, std::size_t offset, std::size_t length, int* out_value) {
    if (offset + length > text.size()) {
        return false;
    }
    int value = 0;
    for (std::size_t i = offset; i < offset + length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (!std::isdigit(ch)) {
            return false;
        }
        value = (value * 10) + (text[i] - '0');
    }
    *out_value = value;
    return true;
}

std::int64_t DaysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned day_of_year =
        (153u * (month + (month > 2 ? static_cast<unsigned>(-3) : 9u)) + 2u) / 5u + day - 1u;
    const unsigned day_of_era =
        year_of_era * 365u + year_of_era / 4u - year_of_era / 100u + day_of_year;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<int>(day_of_era) - 719468;
}

bool ParseIso8601UtcMs(const std::string& text, std::int64_t* out_ms) {
    if (text.size() < 19) {
        return false;
    }
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':') {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (!ParseInt(text, 0, 4, &year) ||
        !ParseInt(text, 5, 2, &month) ||
        !ParseInt(text, 8, 2, &day) ||
        !ParseInt(text, 11, 2, &hour) ||
        !ParseInt(text, 14, 2, &minute) ||
        !ParseInt(text, 17, 2, &second)) {
        return false;
    }

    std::size_t pos = 19;
    int millisecond = 0;
    int ms_digits = 0;
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            if (ms_digits < 3) {
                millisecond = (millisecond * 10) + (text[pos] - '0');
                ++ms_digits;
            }
            ++pos;
        }
    }
    while (ms_digits < 3) {
        millisecond *= 10;
        ++ms_digits;
    }

    int tz_offset_seconds = 0;
    if (pos >= text.size()) {
        return false;
    }

    if (text[pos] == 'Z') {
        ++pos;
    } else if (text[pos] == '+' || text[pos] == '-') {
        const int sign = (text[pos] == '+') ? 1 : -1;
        ++pos;

        int tz_hour = 0;
        int tz_minute = 0;
        if (!ParseInt(text, pos, 2, &tz_hour)) {
            return false;
        }
        pos += 2;
        if (pos < text.size() && text[pos] == ':') {
            ++pos;
        }
        if (!ParseInt(text, pos, 2, &tz_minute)) {
            return false;
        }
        pos += 2;
        tz_offset_seconds = sign * ((tz_hour * 3600) + (tz_minute * 60));
    } else {
        return false;
    }

    if (pos != text.size()) {
        return false;
    }

    const std::int64_t days = DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const std::int64_t seconds =
        days * 86400 + hour * 3600 + minute * 60 + second - tz_offset_seconds;
    *out_ms = (seconds * 1000) + millisecond;
    return true;
}

bool RequireObject(const cJSON* item) {
    return item != nullptr && cJSON_IsObject(item);
}

bool RequireArray(const cJSON* item) {
    return item != nullptr && cJSON_IsArray(item);
}

bool RequireString(const cJSON* item) {
    return item != nullptr && cJSON_IsString(item) && item->valuestring != nullptr;
}

bool RequireNumber(const cJSON* item) {
    return item != nullptr && cJSON_IsNumber(item);
}

bool GetRequiredString(const cJSON* object, const char* key, std::string* out_value, std::string* out_error) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!RequireString(item)) {
        *out_error = std::string("Missing or invalid string field: ") + key;
        return false;
    }
    *out_value = item->valuestring;
    return true;
}

bool GetRequiredInt(const cJSON* object, const char* key, int* out_value, std::string* out_error) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!RequireNumber(item)) {
        *out_error = std::string("Missing or invalid number field: ") + key;
        return false;
    }
    *out_value = item->valueint;
    return true;
}

bool GetRequiredInt64(const cJSON* object, const char* key, std::int64_t* out_value, std::string* out_error) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!RequireNumber(item)) {
        *out_error = std::string("Missing or invalid number field: ") + key;
        return false;
    }
    *out_value = static_cast<std::int64_t>(item->valuedouble);
    return true;
}

std::string JsonCompactString(const cJSON* item) {
    if (item == nullptr) {
        return {};
    }
    char* raw = cJSON_PrintUnformatted(const_cast<cJSON*>(item));
    if (raw == nullptr) {
        return {};
    }
    std::string result(raw);
    cJSON_free(raw);
    return result;
}

bool ParseEnvelope(const cJSON* root, MessageEnvelope* out_envelope, std::string* out_error) {
    std::string sent_at_text;
    if (!GetRequiredInt(root, "schema_version", &out_envelope->schema_version, out_error) ||
        !GetRequiredString(root, "source", &out_envelope->source, out_error) ||
        !GetRequiredString(root, "machine_id", &out_envelope->machine_id, out_error) ||
        !GetRequiredString(root, "sent_at_utc", &sent_at_text, out_error)) {
        return false;
    }

    if (!ParseIso8601UtcMs(sent_at_text, &out_envelope->sent_at_utc_ms)) {
        *out_error = "Invalid sent_at_utc timestamp";
        return false;
    }
    return true;
}

bool ParseHello(const cJSON* root, BridgeMessage* out_message, std::string* out_error) {
    HelloMessage message;
    if (!ParseEnvelope(root, &message.envelope, out_error) ||
        !GetRequiredString(root, "bridge_version", &message.bridge_version, out_error) ||
        !GetRequiredString(root, "hostname", &message.hostname, out_error) ||
        !GetRequiredInt(root, "pid", &message.pid, out_error) ||
        !GetRequiredInt(root, "sample_interval_ms", &message.sample_interval_ms, out_error)) {
        return false;
    }

    message.capabilities_json = JsonCompactString(cJSON_GetObjectItemCaseSensitive(root, "capabilities"));
    *out_message = std::move(message);
    return true;
}

bool ParseCatalogSnapshot(const cJSON* root, BridgeMessage* out_message, std::string* out_error) {
    CatalogSnapshotMessage message;
    if (!ParseEnvelope(root, &message.envelope, out_error)) {
        return false;
    }

    const cJSON* sensors = cJSON_GetObjectItemCaseSensitive(root, "sensors");
    if (!RequireArray(sensors)) {
        *out_error = "Missing or invalid array field: sensors";
        return false;
    }

    const int sensor_count = cJSON_GetArraySize(sensors);
    message.sensors.reserve(sensor_count);

    for (int i = 0; i < sensor_count; ++i) {
        const cJSON* item = cJSON_GetArrayItem(sensors, i);
        if (!RequireObject(item)) {
            *out_error = "Invalid sensor entry in catalog_snapshot";
            return false;
        }

        CatalogSensor sensor;
        if (!GetRequiredString(item, "sensor_uid", &sensor.sensor_uid, out_error) ||
            !GetRequiredString(item, "hardware_uid", &sensor.hardware_uid, out_error) ||
            !GetRequiredString(item, "hardware_path", &sensor.hardware_path, out_error) ||
            !GetRequiredString(item, "hardware_name", &sensor.hardware_name, out_error) ||
            !GetRequiredString(item, "hardware_type", &sensor.hardware_type, out_error) ||
            !GetRequiredString(item, "sensor_path", &sensor.sensor_path, out_error) ||
            !GetRequiredString(item, "sensor_name", &sensor.sensor_name, out_error) ||
            !GetRequiredString(item, "sensor_type", &sensor.sensor_type, out_error) ||
            !GetRequiredInt(item, "sensor_index", &sensor.sensor_index, out_error) ||
            !GetRequiredString(item, "unit", &sensor.unit, out_error)) {
            return false;
        }

        const cJSON* hidden = cJSON_GetObjectItemCaseSensitive(item, "is_default_hidden");
        sensor.is_default_hidden = hidden != nullptr && cJSON_IsTrue(hidden);
        sensor.properties_json = JsonCompactString(cJSON_GetObjectItemCaseSensitive(item, "properties"));

        message.sensors.push_back(std::move(sensor));
    }

    *out_message = std::move(message);
    return true;
}

bool ParseSampleBatch(const cJSON* root, BridgeMessage* out_message, std::string* out_error) {
    SampleBatchMessage message;
    if (!ParseEnvelope(root, &message.envelope, out_error)) {
        return false;
    }

    std::string sample_time_text;
    if (!GetRequiredString(root, "sample_time_utc", &sample_time_text, out_error)) {
        return false;
    }
    if (!ParseIso8601UtcMs(sample_time_text, &message.sample_time_utc_ms)) {
        *out_error = "Invalid sample_time_utc timestamp";
        return false;
    }

    const cJSON* samples = cJSON_GetObjectItemCaseSensitive(root, "samples");
    if (!RequireArray(samples)) {
        *out_error = "Missing or invalid array field: samples";
        return false;
    }

    const int sample_count = cJSON_GetArraySize(samples);
    message.samples.reserve(sample_count);

    for (int i = 0; i < sample_count; ++i) {
        const cJSON* item = cJSON_GetArrayItem(samples, i);
        if (!RequireObject(item)) {
            *out_error = "Invalid sample entry in sample_batch";
            return false;
        }

        SensorSample sample;
        if (!GetRequiredString(item, "sensor_uid", &sample.sensor_uid, out_error) ||
            !GetRequiredString(item, "quality", &sample.quality, out_error)) {
            return false;
        }

        const cJSON* value = cJSON_GetObjectItemCaseSensitive(item, "value");
        if (!RequireNumber(value)) {
            *out_error = "Missing or invalid number field: value";
            return false;
        }
        sample.value = value->valuedouble;

        const cJSON* min_value = cJSON_GetObjectItemCaseSensitive(item, "min");
        if (RequireNumber(min_value)) {
            sample.min_value = min_value->valuedouble;
        }

        const cJSON* max_value = cJSON_GetObjectItemCaseSensitive(item, "max");
        if (RequireNumber(max_value)) {
            sample.max_value = max_value->valuedouble;
        }

        message.samples.push_back(std::move(sample));
    }

    *out_message = std::move(message);
    return true;
}

bool ParseHeartbeat(const cJSON* root, BridgeMessage* out_message, std::string* out_error) {
    HeartbeatMessage message;
    if (!ParseEnvelope(root, &message.envelope, out_error) ||
        !GetRequiredString(root, "status", &message.status, out_error) ||
        !GetRequiredInt64(root, "uptime_s", &message.uptime_seconds, out_error) ||
        !GetRequiredInt(root, "catalog_count", &message.catalog_count, out_error)) {
        return false;
    }

    *out_message = std::move(message);
    return true;
}

}  // namespace

bool ParseBridgeMessage(std::string_view json_line,
                        model::BridgeMessage* out_message,
                        std::string* out_error) {
    if (out_message == nullptr || out_error == nullptr) {
        return false;
    }

    std::string payload(json_line);
    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        *out_error = "Invalid JSON payload";
        return false;
    }

    bool ok = false;
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!RequireString(type)) {
        *out_error = "Missing or invalid string field: type";
    } else if (std::string(type->valuestring) == "hello") {
        ok = ParseHello(root, out_message, out_error);
    } else if (std::string(type->valuestring) == "catalog_snapshot") {
        ok = ParseCatalogSnapshot(root, out_message, out_error);
    } else if (std::string(type->valuestring) == "sample_batch") {
        ok = ParseSampleBatch(root, out_message, out_error);
    } else if (std::string(type->valuestring) == "heartbeat") {
        ok = ParseHeartbeat(root, out_message, out_error);
    } else {
        *out_error = std::string("Unsupported message type: ") + type->valuestring;
    }

    cJSON_Delete(root);
    return ok;
}

}  // namespace host_agent::core

