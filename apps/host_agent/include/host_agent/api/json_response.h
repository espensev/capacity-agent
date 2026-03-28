#ifndef HOST_AGENT_API_JSON_RESPONSE_H
#define HOST_AGENT_API_JSON_RESPONSE_H

#include <cJSON.h>

#include <memory>
#include <optional>
#include <string>

namespace host_agent::api {

struct CJsonDeleter {
    void operator()(cJSON* ptr) const {
        if (ptr) cJSON_Delete(ptr);
    }
};

using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

inline CJsonPtr MakeObject() {
    return CJsonPtr(cJSON_CreateObject());
}

inline CJsonPtr MakeArray() {
    return CJsonPtr(cJSON_CreateArray());
}

inline std::string PrintJson(const cJSON* json) {
    char* raw = cJSON_PrintUnformatted(json);
    if (!raw) return "{}";
    std::string result(raw);
    cJSON_free(raw);
    return result;
}

inline void AddString(cJSON* obj, const char* key, const std::string& value) {
    cJSON_AddStringToObject(obj, key, value.c_str());
}

inline void AddInt(cJSON* obj, const char* key, int value) {
    cJSON_AddNumberToObject(obj, key, value);
}

inline void AddInt64(cJSON* obj, const char* key, std::int64_t value) {
    cJSON_AddNumberToObject(obj, key, static_cast<double>(value));
}

inline void AddDouble(cJSON* obj, const char* key, double value) {
    cJSON_AddNumberToObject(obj, key, value);
}

inline void AddBool(cJSON* obj, const char* key, bool value) {
    cJSON_AddBoolToObject(obj, key, value ? 1 : 0);
}

inline void AddNull(cJSON* obj, const char* key) {
    cJSON_AddNullToObject(obj, key);
}

inline void AddOptionalDouble(cJSON* obj, const char* key, const std::optional<double>& value) {
    if (value.has_value()) {
        AddDouble(obj, key, *value);
    } else {
        AddNull(obj, key);
    }
}

inline void AddOptionalInt64(cJSON* obj, const char* key, const std::optional<std::int64_t>& value) {
    if (value.has_value()) {
        AddInt64(obj, key, *value);
    } else {
        AddNull(obj, key);
    }
}

inline void AddOptionalString(cJSON* obj, const char* key, const std::optional<std::string>& value) {
    if (value.has_value()) {
        AddString(obj, key, *value);
    } else {
        AddNull(obj, key);
    }
}

}  // namespace host_agent::api

#endif  // HOST_AGENT_API_JSON_RESPONSE_H
