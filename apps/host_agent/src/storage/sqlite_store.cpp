#include "host_agent/storage/sqlite_store.h"

#include "sqlite3.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace host_agent::storage {

namespace {

void LogSqliteError(sqlite3* db, std::string_view context, int rc) {
    std::cerr << context << " failed: (" << rc << ") "
              << (db != nullptr ? sqlite3_errmsg(db) : "no database handle") << "\n";
}

bool ExecSql(sqlite3* db, std::string_view sql, std::string_view context) {
    char* error_message = nullptr;
    const int rc = sqlite3_exec(db, std::string(sql).c_str(), nullptr, nullptr, &error_message);
    if (rc == SQLITE_OK) {
        return true;
    }

    std::cerr << context << " failed: (" << rc << ") "
              << (error_message != nullptr ? error_message : sqlite3_errmsg(db)) << "\n";
    if (error_message != nullptr) {
        sqlite3_free(error_message);
    }
    return false;
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string JsonEscape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 8);

    for (const unsigned char ch : text) {
        switch (ch) {
            case '\"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(ch));
                    escaped += buffer;
                } else {
                    escaped.push_back(static_cast<char>(ch));
                }
                break;
        }
    }

    return escaped;
}

std::string JsonString(std::string_view text) {
    return "\"" + JsonEscape(text) + "\"";
}

std::string CollectorId(std::string_view machine_id, std::string_view source) {
    return std::string(machine_id) + ":" + std::string(source);
}

std::string BuildMachineExtraJson(const model::HelloMessage& message) {
    std::ostringstream json;
    json << "{\"bridge_source\":" << JsonString(message.envelope.source)
         << ",\"bridge_version\":" << JsonString(message.bridge_version)
         << "}";
    return json.str();
}

std::string BuildHelloExtraJson(const model::HelloMessage& message) {
    std::ostringstream json;
    json << "{\"hostname\":" << JsonString(message.hostname)
         << ",\"bridge_version\":" << JsonString(message.bridge_version)
         << ",\"pid\":" << message.pid
         << ",\"sample_interval_ms\":" << message.sample_interval_ms;

    if (!message.capabilities_json.empty()) {
        json << ",\"capabilities\":" << message.capabilities_json;
    }

    json << "}";
    return json.str();
}

std::string BuildCatalogExtraJson(std::size_t sensor_count) {
    return "{\"sensor_count\":" + std::to_string(sensor_count) + "}";
}

std::string BuildSampleExtraJson(std::size_t sample_count) {
    return "{\"sample_count\":" + std::to_string(sample_count) + "}";
}

std::string BuildHeartbeatExtraJson(const model::HeartbeatMessage& message) {
    return "{\"uptime_s\":" + std::to_string(message.uptime_seconds) +
           ",\"catalog_count\":" + std::to_string(message.catalog_count) + "}";
}

class Statement {
public:
    Statement(sqlite3* db, std::string_view sql, std::string_view context)
        : db_(db),
          context_(context) {
        const int rc = sqlite3_prepare_v2(db_, std::string(sql).c_str(), -1, &statement_, nullptr);
        if (rc != SQLITE_OK) {
            LogSqliteError(db_, context_, rc);
        }
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool ok() const {
        return statement_ != nullptr;
    }

    bool BindText(int index, const std::string& value) {
        return Check(sqlite3_bind_text(statement_, index, value.c_str(), -1, SQLITE_TRANSIENT),
                     "bind text");
    }

    bool BindText(int index, std::string_view value) {
        return Check(sqlite3_bind_text(
                         statement_,
                         index,
                         value.data(),
                         static_cast<int>(value.size()),
                         SQLITE_TRANSIENT),
                     "bind text");
    }

    bool BindNullableText(int index, const std::optional<std::string>& value) {
        if (!value.has_value()) {
            return BindNull(index);
        }
        return BindText(index, *value);
    }

    bool BindInt(int index, int value) {
        return Check(sqlite3_bind_int(statement_, index, value), "bind int");
    }

    bool BindInt64(int index, std::int64_t value) {
        return Check(sqlite3_bind_int64(statement_, index, value), "bind int64");
    }

    bool BindNullableInt64(int index, const std::optional<std::int64_t>& value) {
        if (!value.has_value()) {
            return BindNull(index);
        }
        return BindInt64(index, *value);
    }

    bool BindDouble(int index, double value) {
        return Check(sqlite3_bind_double(statement_, index, value), "bind double");
    }

    bool BindNullableDouble(int index, const std::optional<double>& value) {
        if (!value.has_value()) {
            return BindNull(index);
        }
        return BindDouble(index, *value);
    }

    bool BindNull(int index) {
        return Check(sqlite3_bind_null(statement_, index), "bind null");
    }

    bool StepDone() {
        const int rc = sqlite3_step(statement_);
        if (rc == SQLITE_DONE) {
            return true;
        }

        LogSqliteError(db_, context_, rc);
        return false;
    }

    bool StepRow() {
        const int rc = sqlite3_step(statement_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        LogSqliteError(db_, context_, rc);
        return false;
    }

    bool Reset() {
        return sqlite3_reset(statement_) == SQLITE_OK;
    }

    std::string ColumnText(int col) {
        const unsigned char* text = sqlite3_column_text(statement_, col);
        return text != nullptr ? std::string(reinterpret_cast<const char*>(text)) : std::string{};
    }

    int ColumnInt(int col) {
        return sqlite3_column_int(statement_, col);
    }

    std::int64_t ColumnInt64(int col) {
        return sqlite3_column_int64(statement_, col);
    }

    double ColumnDouble(int col) {
        return sqlite3_column_double(statement_, col);
    }

    bool ColumnIsNull(int col) {
        return sqlite3_column_type(statement_, col) == SQLITE_NULL;
    }

    std::optional<std::string> ColumnOptionalText(int col) {
        if (ColumnIsNull(col)) return std::nullopt;
        return ColumnText(col);
    }

    std::optional<std::int64_t> ColumnOptionalInt64(int col) {
        if (ColumnIsNull(col)) return std::nullopt;
        return ColumnInt64(col);
    }

    std::optional<double> ColumnOptionalDouble(int col) {
        if (ColumnIsNull(col)) return std::nullopt;
        return ColumnDouble(col);
    }

    std::optional<int> ColumnOptionalInt(int col) {
        if (ColumnIsNull(col)) return std::nullopt;
        return ColumnInt(col);
    }

private:
    bool Check(int rc, std::string_view action) {
        if (rc == SQLITE_OK) {
            return true;
        }

        LogSqliteError(db_, std::string(context_) + " " + std::string(action), rc);
        return false;
    }

    sqlite3* db_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
    std::string_view context_;
};

class Transaction {
public:
    explicit Transaction(sqlite3* db)
        : db_(db) {}

    bool Begin() {
        active_ = ExecSql(db_, "BEGIN IMMEDIATE", "BEGIN IMMEDIATE");
        return active_;
    }

    bool Commit() {
        if (!active_) {
            return false;
        }
        if (!ExecSql(db_, "COMMIT", "COMMIT")) {
            return false;
        }
        active_ = false;
        return true;
    }

    ~Transaction() {
        if (active_) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

private:
    sqlite3* db_ = nullptr;
    bool active_ = false;
};

bool UpsertMachine(sqlite3* db,
                   const std::string& machine_id,
                   const std::optional<std::string>& hostname,
                   const std::optional<std::string>& display_name,
                   std::int64_t seen_utc_ms,
                   const std::optional<std::string>& extra_json) {
    static constexpr std::string_view kSql = R"sql(
        INSERT INTO machine_catalog (
            machine_id,
            hostname,
            display_name,
            first_seen_utc_ms,
            last_seen_utc_ms,
            extra_json
        ) VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(machine_id) DO UPDATE SET
            hostname = COALESCE(excluded.hostname, machine_catalog.hostname),
            display_name = COALESCE(excluded.display_name, machine_catalog.display_name),
            last_seen_utc_ms = CASE
                WHEN excluded.last_seen_utc_ms > machine_catalog.last_seen_utc_ms
                    THEN excluded.last_seen_utc_ms
                ELSE machine_catalog.last_seen_utc_ms
            END,
            extra_json = COALESCE(excluded.extra_json, machine_catalog.extra_json);
    )sql";

    Statement statement(db, kSql, "upsert machine_catalog");
    if (!statement.ok() ||
        !statement.BindText(1, machine_id) ||
        !statement.BindNullableText(2, hostname) ||
        !statement.BindNullableText(3, display_name) ||
        !statement.BindInt64(4, seen_utc_ms) ||
        !statement.BindInt64(5, seen_utc_ms) ||
        !statement.BindNullableText(6, extra_json)) {
        return false;
    }

    return statement.StepDone();
}

bool UpsertCollector(sqlite3* db,
                     const std::string& collector_id,
                     const std::string& machine_id,
                     const std::string& source,
                     const std::string& collector_type,
                     const std::string& status,
                     const std::optional<std::int64_t>& last_hello_utc_ms,
                     const std::optional<std::int64_t>& last_sample_utc_ms,
                     const std::optional<std::string>& last_error,
                     std::int64_t updated_utc_ms,
                     const std::optional<std::string>& extra_json) {
    static constexpr std::string_view kSql = R"sql(
        INSERT INTO collector_health (
            collector_id,
            machine_id,
            source,
            collector_type,
            status,
            last_hello_utc_ms,
            last_sample_utc_ms,
            last_error,
            updated_utc_ms,
            extra_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(collector_id) DO UPDATE SET
            machine_id = excluded.machine_id,
            source = excluded.source,
            collector_type = excluded.collector_type,
            status = excluded.status,
            last_hello_utc_ms = CASE
                WHEN excluded.last_hello_utc_ms IS NULL THEN collector_health.last_hello_utc_ms
                WHEN collector_health.last_hello_utc_ms IS NULL THEN excluded.last_hello_utc_ms
                WHEN excluded.last_hello_utc_ms > collector_health.last_hello_utc_ms
                    THEN excluded.last_hello_utc_ms
                ELSE collector_health.last_hello_utc_ms
            END,
            last_sample_utc_ms = CASE
                WHEN excluded.last_sample_utc_ms IS NULL THEN collector_health.last_sample_utc_ms
                WHEN collector_health.last_sample_utc_ms IS NULL THEN excluded.last_sample_utc_ms
                WHEN excluded.last_sample_utc_ms > collector_health.last_sample_utc_ms
                    THEN excluded.last_sample_utc_ms
                ELSE collector_health.last_sample_utc_ms
            END,
            last_error = excluded.last_error,
            updated_utc_ms = excluded.updated_utc_ms,
            extra_json = COALESCE(excluded.extra_json, collector_health.extra_json);
    )sql";

    Statement statement(db, kSql, "upsert collector_health");
    if (!statement.ok() ||
        !statement.BindText(1, collector_id) ||
        !statement.BindText(2, machine_id) ||
        !statement.BindText(3, source) ||
        !statement.BindText(4, collector_type) ||
        !statement.BindText(5, status) ||
        !statement.BindNullableInt64(6, last_hello_utc_ms) ||
        !statement.BindNullableInt64(7, last_sample_utc_ms) ||
        !statement.BindNullableText(8, last_error) ||
        !statement.BindInt64(9, updated_utc_ms) ||
        !statement.BindNullableText(10, extra_json)) {
        return false;
    }

    return statement.StepDone();
}

bool UpsertCatalogSensor(sqlite3* db,
                         const model::CatalogSnapshotMessage& message,
                         const model::CatalogSensor& sensor) {
    static constexpr std::string_view kSql = R"sql(
        INSERT INTO sensor_catalog (
            sensor_uid,
            machine_id,
            source,
            hardware_uid,
            hardware_path,
            hardware_name,
            hardware_type,
            sensor_path,
            sensor_name,
            sensor_type,
            sensor_index,
            unit,
            is_default_hidden,
            first_seen_utc_ms,
            last_seen_utc_ms,
            properties_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(sensor_uid) DO UPDATE SET
            machine_id = excluded.machine_id,
            source = excluded.source,
            hardware_uid = excluded.hardware_uid,
            hardware_path = excluded.hardware_path,
            hardware_name = excluded.hardware_name,
            hardware_type = excluded.hardware_type,
            sensor_path = excluded.sensor_path,
            sensor_name = excluded.sensor_name,
            sensor_type = excluded.sensor_type,
            sensor_index = excluded.sensor_index,
            unit = excluded.unit,
            is_default_hidden = excluded.is_default_hidden,
            last_seen_utc_ms = excluded.last_seen_utc_ms,
            properties_json = COALESCE(excluded.properties_json, sensor_catalog.properties_json);
    )sql";

    Statement statement(db, kSql, "upsert sensor_catalog");
    if (!statement.ok() ||
        !statement.BindText(1, sensor.sensor_uid) ||
        !statement.BindText(2, message.envelope.machine_id) ||
        !statement.BindText(3, message.envelope.source) ||
        !statement.BindText(4, sensor.hardware_uid) ||
        !statement.BindText(5, sensor.hardware_path) ||
        !statement.BindText(6, sensor.hardware_name) ||
        !statement.BindText(7, sensor.hardware_type) ||
        !statement.BindText(8, sensor.sensor_path) ||
        !statement.BindText(9, sensor.sensor_name) ||
        !statement.BindText(10, sensor.sensor_type) ||
        !statement.BindInt(11, sensor.sensor_index) ||
        !statement.BindText(12, sensor.unit) ||
        !statement.BindInt(13, sensor.is_default_hidden ? 1 : 0) ||
        !statement.BindInt64(14, message.envelope.sent_at_utc_ms) ||
        !statement.BindInt64(15, message.envelope.sent_at_utc_ms) ||
        !statement.BindNullableText(
            16,
            sensor.properties_json.empty()
                ? std::optional<std::string>{}
                : std::optional<std::string>{sensor.properties_json})) {
        return false;
    }

    return statement.StepDone();
}

bool InsertSensorSample(sqlite3* db,
                        const model::SampleBatchMessage& message,
                        const model::SensorSample& sample) {
    static constexpr std::string_view kSql = R"sql(
        INSERT OR REPLACE INTO sensor_samples (
            sensor_uid,
            ts_utc_ms,
            value,
            min_value,
            max_value,
            quality,
            source,
            extra_json
        )
        SELECT ?, ?, ?, ?, ?, ?, ?, NULL
        WHERE EXISTS (
            SELECT 1 FROM sensor_catalog WHERE sensor_uid = ?
        );
    )sql";

    Statement statement(db, kSql, "insert sensor_samples");
    if (!statement.ok() ||
        !statement.BindText(1, sample.sensor_uid) ||
        !statement.BindInt64(2, message.sample_time_utc_ms) ||
        !statement.BindDouble(3, sample.value) ||
        !statement.BindNullableDouble(4, sample.min_value) ||
        !statement.BindNullableDouble(5, sample.max_value) ||
        !statement.BindText(6, sample.quality) ||
        !statement.BindText(7, message.envelope.source) ||
        !statement.BindText(8, sample.sensor_uid)) {
        return false;
    }

    return statement.StepDone();
}

}  // namespace

SqliteStore::SqliteStore(std::filesystem::path sqlite_path, std::filesystem::path schema_path)
    : sqlite_path_(std::move(sqlite_path)),
      schema_path_(std::move(schema_path)) {}

SqliteStore::~SqliteStore() {
    Close();
}

bool SqliteStore::OpenIfNeeded() {
    if (db_ != nullptr) {
        return true;
    }

    const std::filesystem::path parent = sqlite_path_.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            std::cerr << "Failed to create SQLite directory " << parent.string()
                      << ": " << error.message() << "\n";
            return false;
        }
    }

    const int rc = sqlite3_open_v2(
        sqlite_path_.string().c_str(),
        &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        LogSqliteError(db_, "sqlite3_open_v2", rc);
        Close();
        return false;
    }

    sqlite3_busy_timeout(db_, 5000);

    return true;
}

void SqliteStore::Close() {
    if (db_ == nullptr) {
        return;
    }

    sqlite3_close(db_);
    db_ = nullptr;
}

bool SqliteStore::EnsureSchema() {
    if (!OpenIfNeeded()) {
        return false;
    }

    if (!std::filesystem::exists(schema_path_)) {
        std::cerr << "Schema file does not exist: " << schema_path_.string() << "\n";
        return false;
    }

    const std::string schema = ReadTextFile(schema_path_);
    if (schema.empty()) {
        std::cerr << "Schema file is empty or unreadable: " << schema_path_.string() << "\n";
        return false;
    }

    return ExecSql(db_, schema, "apply schema");
}

bool SqliteStore::UpsertHello(const model::HelloMessage& message) {
    if (!OpenIfNeeded()) {
        return false;
    }

    Transaction transaction(db_);
    if (!transaction.Begin()) {
        return false;
    }

    if (!UpsertMachine(
            db_,
            message.envelope.machine_id,
            std::optional<std::string>{message.hostname},
            std::optional<std::string>{message.hostname},
            message.envelope.sent_at_utc_ms,
            std::optional<std::string>{BuildMachineExtraJson(message)}) ||
        !UpsertCollector(
            db_,
            CollectorId(message.envelope.machine_id, message.envelope.source),
            message.envelope.machine_id,
            message.envelope.source,
            "bridge",
            "ok",
            std::optional<std::int64_t>{message.envelope.sent_at_utc_ms},
            std::optional<std::int64_t>{},
            std::optional<std::string>{},
            message.envelope.sent_at_utc_ms,
            std::optional<std::string>{BuildHelloExtraJson(message)})) {
        return false;
    }

    return transaction.Commit();
}

bool SqliteStore::UpsertCatalogSnapshot(const model::CatalogSnapshotMessage& message) {
    if (!OpenIfNeeded()) {
        return false;
    }

    Transaction transaction(db_);
    if (!transaction.Begin()) {
        return false;
    }

    if (!UpsertMachine(
            db_,
            message.envelope.machine_id,
            std::optional<std::string>{},
            std::optional<std::string>{},
            message.envelope.sent_at_utc_ms,
            std::optional<std::string>{}) ||
        !UpsertCollector(
            db_,
            CollectorId(message.envelope.machine_id, message.envelope.source),
            message.envelope.machine_id,
            message.envelope.source,
            "sensor_stream",
            "ok",
            std::optional<std::int64_t>{},
            std::optional<std::int64_t>{},
            std::optional<std::string>{},
            message.envelope.sent_at_utc_ms,
            std::optional<std::string>{BuildCatalogExtraJson(message.sensors.size())})) {
        return false;
    }

    for (const model::CatalogSensor& sensor : message.sensors) {
        if (!UpsertCatalogSensor(db_, message, sensor)) {
            return false;
        }
    }

    return transaction.Commit();
}

bool SqliteStore::InsertSampleBatch(const model::SampleBatchMessage& message) {
    if (!OpenIfNeeded()) {
        return false;
    }

    Transaction transaction(db_);
    if (!transaction.Begin()) {
        return false;
    }

    if (!UpsertMachine(
            db_,
            message.envelope.machine_id,
            std::optional<std::string>{},
            std::optional<std::string>{},
            message.sample_time_utc_ms,
            std::optional<std::string>{}) ||
        !UpsertCollector(
            db_,
            CollectorId(message.envelope.machine_id, message.envelope.source),
            message.envelope.machine_id,
            message.envelope.source,
            "sensor_stream",
            "ok",
            std::optional<std::int64_t>{},
            std::optional<std::int64_t>{message.sample_time_utc_ms},
            std::optional<std::string>{},
            message.envelope.sent_at_utc_ms,
            std::optional<std::string>{BuildSampleExtraJson(message.samples.size())})) {
        return false;
    }

    for (const model::SensorSample& sample : message.samples) {
        if (!InsertSensorSample(db_, message, sample)) {
            return false;
        }
    }

    return transaction.Commit();
}

bool SqliteStore::UpsertHeartbeat(const model::HeartbeatMessage& message) {
    if (!OpenIfNeeded()) {
        return false;
    }

    Transaction transaction(db_);
    if (!transaction.Begin()) {
        return false;
    }

    if (!UpsertMachine(
            db_,
            message.envelope.machine_id,
            std::optional<std::string>{},
            std::optional<std::string>{},
            message.envelope.sent_at_utc_ms,
            std::optional<std::string>{}) ||
        !UpsertCollector(
            db_,
            CollectorId(message.envelope.machine_id, message.envelope.source),
            message.envelope.machine_id,
            message.envelope.source,
            "bridge",
            message.status,
            std::optional<std::int64_t>{},
            std::optional<std::int64_t>{},
            std::optional<std::string>{},
            message.envelope.sent_at_utc_ms,
            std::optional<std::string>{BuildHeartbeatExtraJson(message)})) {
        return false;
    }

    return transaction.Commit();
}

// -- Collector write methods (for GPU / Ollama collectors) --

bool SqliteStore::InsertGpuSample(
    const std::string& machine_id, const std::string& gpu_uid,
    int gpu_index, std::int64_t ts_utc_ms,
    const std::optional<std::string>& gpu_name,
    const std::string& backend,
    const std::optional<double>& core_c,
    const std::optional<double>& hotspot_c,
    const std::optional<double>& mem_junction_c,
    const std::optional<double>& util_gpu_pct,
    const std::optional<double>& util_mem_pct,
    const std::optional<double>& power_w,
    const std::optional<std::int64_t>& vram_used_bytes,
    const std::optional<std::int64_t>& vram_total_bytes,
    const std::optional<int>& clock_graphics_mhz,
    const std::optional<int>& clock_memory_mhz,
    const std::optional<int>& fan0_rpm,
    const std::optional<double>& fan0_pct) {
    if (!OpenIfNeeded()) return false;

    static constexpr std::string_view kSql = R"sql(
        INSERT OR REPLACE INTO gpu_samples (
            machine_id, gpu_uid, gpu_index, ts_utc_ms, gpu_name, backend,
            core_c, hotspot_c, mem_junction_c, util_gpu_pct, util_mem_pct,
            power_w, vram_used_bytes, vram_total_bytes,
            clock_graphics_mhz, clock_memory_mhz, fan0_rpm, fan0_pct
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )sql";

    Statement stmt(db_, kSql, "insert gpu_samples");
    if (!stmt.ok()) return false;

    return stmt.BindText(1, machine_id) &&
           stmt.BindText(2, gpu_uid) &&
           stmt.BindInt(3, gpu_index) &&
           stmt.BindInt64(4, ts_utc_ms) &&
           stmt.BindNullableText(5, gpu_name) &&
           stmt.BindText(6, backend) &&
           stmt.BindNullableDouble(7, core_c) &&
           stmt.BindNullableDouble(8, hotspot_c) &&
           stmt.BindNullableDouble(9, mem_junction_c) &&
           stmt.BindNullableDouble(10, util_gpu_pct) &&
           stmt.BindNullableDouble(11, util_mem_pct) &&
           stmt.BindNullableDouble(12, power_w) &&
           stmt.BindNullableInt64(13, vram_used_bytes) &&
           stmt.BindNullableInt64(14, vram_total_bytes) &&
           stmt.BindNullableInt64(15, clock_graphics_mhz ? std::optional<std::int64_t>{*clock_graphics_mhz} : std::nullopt) &&
           stmt.BindNullableInt64(16, clock_memory_mhz ? std::optional<std::int64_t>{*clock_memory_mhz} : std::nullopt) &&
           stmt.BindNullableInt64(17, fan0_rpm ? std::optional<std::int64_t>{*fan0_rpm} : std::nullopt) &&
           stmt.BindNullableDouble(18, fan0_pct) &&
           stmt.StepDone();
}

bool SqliteStore::InsertOllamaRuntimeSample(
    const std::string& machine_id,
    std::int64_t ts_utc_ms,
    const std::string& base_url,
    const std::optional<int>& active_requests,
    const std::optional<int>& queued_requests,
    const std::optional<int>& loaded_models,
    const std::optional<std::int64_t>& resident_vram_bytes,
    const std::optional<std::int64_t>& resident_ram_bytes) {
    if (!OpenIfNeeded()) return false;

    static constexpr std::string_view kSql = R"sql(
        INSERT OR REPLACE INTO ollama_runtime_samples (
            machine_id, ts_utc_ms, base_url,
            active_request_count, queued_request_count, loaded_model_count,
            resident_vram_bytes, resident_ram_bytes
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )sql";

    Statement stmt(db_, kSql, "insert ollama_runtime_samples");
    if (!stmt.ok()) return false;

    return stmt.BindText(1, machine_id) &&
           stmt.BindInt64(2, ts_utc_ms) &&
           stmt.BindText(3, base_url) &&
           stmt.BindNullableInt64(4, active_requests ? std::optional<std::int64_t>{*active_requests} : std::nullopt) &&
           stmt.BindNullableInt64(5, queued_requests ? std::optional<std::int64_t>{*queued_requests} : std::nullopt) &&
           stmt.BindNullableInt64(6, loaded_models ? std::optional<std::int64_t>{*loaded_models} : std::nullopt) &&
           stmt.BindNullableInt64(7, resident_vram_bytes) &&
           stmt.BindNullableInt64(8, resident_ram_bytes) &&
           stmt.StepDone();
}

bool SqliteStore::UpsertCollectorHealthDirect(
    const std::string& machine_id,
    const std::string& source,
    const std::string& collector_type,
    const std::string& status,
    std::int64_t now_utc_ms,
    const std::optional<std::string>& last_error) {
    if (!OpenIfNeeded()) return false;

    return UpsertCollector(
        db_,
        CollectorId(machine_id, source),
        machine_id,
        source,
        collector_type,
        status,
        std::nullopt,
        std::optional<std::int64_t>{now_utc_ms},
        last_error,
        now_utc_ms,
        std::nullopt);
}

// -- Read query methods --

std::vector<CollectorHealthRow> SqliteStore::QueryCollectorHealth(const std::string& machine_id) {
    std::vector<CollectorHealthRow> result;
    if (!OpenIfNeeded()) return result;

    static constexpr std::string_view kSql = R"sql(
        SELECT collector_id, machine_id, source, collector_type, status,
               last_hello_utc_ms, last_sample_utc_ms, last_error, updated_utc_ms
        FROM collector_health
        WHERE machine_id = ?
    )sql";

    Statement stmt(db_, kSql, "query collector_health");
    if (!stmt.ok() || !stmt.BindText(1, machine_id)) return result;

    while (stmt.StepRow()) {
        CollectorHealthRow row;
        row.collector_id = stmt.ColumnText(0);
        row.machine_id = stmt.ColumnText(1);
        row.source = stmt.ColumnText(2);
        row.collector_type = stmt.ColumnText(3);
        row.status = stmt.ColumnText(4);
        row.last_hello_utc_ms = stmt.ColumnOptionalInt64(5);
        row.last_sample_utc_ms = stmt.ColumnOptionalInt64(6);
        row.last_error = stmt.ColumnOptionalText(7);
        row.updated_utc_ms = stmt.ColumnInt64(8);
        result.push_back(std::move(row));
    }

    return result;
}

std::optional<MachineRow> SqliteStore::QueryMachineCatalog(const std::string& machine_id) {
    if (!OpenIfNeeded()) return std::nullopt;

    static constexpr std::string_view kSql = R"sql(
        SELECT machine_id, hostname, display_name, first_seen_utc_ms, last_seen_utc_ms
        FROM machine_catalog
        WHERE machine_id = ?
    )sql";

    Statement stmt(db_, kSql, "query machine_catalog");
    if (!stmt.ok() || !stmt.BindText(1, machine_id)) return std::nullopt;

    if (!stmt.StepRow()) return std::nullopt;

    MachineRow row;
    row.machine_id = stmt.ColumnText(0);
    row.hostname = stmt.ColumnText(1);
    row.display_name = stmt.ColumnText(2);
    row.first_seen_utc_ms = stmt.ColumnInt64(3);
    row.last_seen_utc_ms = stmt.ColumnInt64(4);
    return row;
}

std::vector<SensorCatalogRow> SqliteStore::QuerySensorCatalog(
    const std::string& machine_id, const CatalogFilter& filter) {
    std::vector<SensorCatalogRow> result;
    if (!OpenIfNeeded()) return result;

    std::string sql =
        "SELECT sensor_uid, hardware_name, hardware_type, sensor_name, "
        "sensor_type, unit, sensor_index, is_default_hidden "
        "FROM sensor_catalog WHERE machine_id = ?";

    int param_index = 2;
    if (filter.hardware_type.has_value()) {
        sql += " AND hardware_type = ?";
    }
    if (filter.sensor_type.has_value()) {
        sql += " AND sensor_type = ?";
    }
    sql += " ORDER BY hardware_type, sensor_type, sensor_name";

    Statement stmt(db_, sql, "query sensor_catalog");
    if (!stmt.ok() || !stmt.BindText(1, machine_id)) return result;

    if (filter.hardware_type.has_value()) {
        if (!stmt.BindText(param_index++, *filter.hardware_type)) return result;
    }
    if (filter.sensor_type.has_value()) {
        if (!stmt.BindText(param_index++, *filter.sensor_type)) return result;
    }

    while (stmt.StepRow()) {
        SensorCatalogRow row;
        row.sensor_uid = stmt.ColumnText(0);
        row.hardware_name = stmt.ColumnText(1);
        row.hardware_type = stmt.ColumnText(2);
        row.sensor_name = stmt.ColumnText(3);
        row.sensor_type = stmt.ColumnText(4);
        row.unit = stmt.ColumnText(5);
        row.sensor_index = stmt.ColumnInt(6);
        row.is_default_hidden = stmt.ColumnInt(7) != 0;
        result.push_back(std::move(row));
    }

    return result;
}

std::vector<SensorSampleRow> SqliteStore::QuerySensorHistory(
    const std::string& sensor_uid, std::int64_t from_ms, std::int64_t to_ms, int limit) {
    std::vector<SensorSampleRow> result;
    if (!OpenIfNeeded()) return result;

    static constexpr std::string_view kSql = R"sql(
        SELECT ts_utc_ms, value, min_value, max_value, quality
        FROM sensor_samples
        WHERE sensor_uid = ? AND ts_utc_ms BETWEEN ? AND ?
        ORDER BY ts_utc_ms DESC
        LIMIT ?
    )sql";

    Statement stmt(db_, kSql, "query sensor_history");
    if (!stmt.ok() ||
        !stmt.BindText(1, sensor_uid) ||
        !stmt.BindInt64(2, from_ms) ||
        !stmt.BindInt64(3, to_ms) ||
        !stmt.BindInt(4, limit)) {
        return result;
    }

    while (stmt.StepRow()) {
        SensorSampleRow row;
        row.ts_utc_ms = stmt.ColumnInt64(0);
        row.value = stmt.ColumnDouble(1);
        row.min_value = stmt.ColumnOptionalDouble(2);
        row.max_value = stmt.ColumnOptionalDouble(3);
        row.quality = stmt.ColumnText(4);
        result.push_back(std::move(row));
    }

    return result;
}

LatestSnapshot SqliteStore::QueryLatestSnapshot(const std::string& machine_id) {
    LatestSnapshot snapshot;
    if (!OpenIfNeeded()) return snapshot;

    // System metrics from latest_sensor_samples via sensor_catalog
    {
        static constexpr std::string_view kSql = R"sql(
            SELECT sc.sensor_name, sc.sensor_type, sc.hardware_type, lss.value
            FROM latest_sensor_samples lss
            JOIN sensor_catalog sc ON sc.sensor_uid = lss.sensor_uid
            WHERE sc.machine_id = ?
              AND sc.hardware_type IN ('Cpu', 'Memory')
              AND sc.sensor_type IN ('Temperature', 'Load', 'Data')
        )sql";

        Statement stmt(db_, kSql, "query snapshot system");
        if (stmt.ok() && stmt.BindText(1, machine_id)) {
            while (stmt.StepRow()) {
                const std::string name = stmt.ColumnText(0);
                const std::string sensor_type = stmt.ColumnText(1);
                const std::string hw_type = stmt.ColumnText(2);
                const double value = stmt.ColumnDouble(3);

                if (hw_type == "Cpu" && sensor_type == "Temperature") {
                    // Take the first CPU temp as package temp
                    if (!snapshot.cpu_package_c.has_value()) {
                        snapshot.cpu_package_c = value;
                    }
                } else if (hw_type == "Cpu" && sensor_type == "Load" &&
                           name.find("Total") != std::string::npos) {
                    snapshot.cpu_total_load_pct = value;
                } else if (hw_type == "Memory" && sensor_type == "Load") {
                    // Memory load percentage — convert to bytes if total is known
                    // For now store as-is; enrichment happens when system_samples is populated
                } else if (hw_type == "Memory" && sensor_type == "Data") {
                    if (name.find("Used") != std::string::npos) {
                        snapshot.memory_used_bytes = static_cast<std::int64_t>(value * 1073741824.0);
                    } else if (name.find("Available") != std::string::npos) {
                        // Could derive total from used + available
                    }
                }
            }
        }
    }

    // GPU metrics from gpu_samples (latest per GPU)
    {
        static constexpr std::string_view kSql = R"sql(
            SELECT gpu_index, gpu_name, core_c, hotspot_c,
                   util_gpu_pct, vram_used_bytes, vram_total_bytes, power_w
            FROM gpu_samples
            WHERE machine_id = ?
              AND ts_utc_ms = (
                  SELECT MAX(ts_utc_ms) FROM gpu_samples WHERE machine_id = ?
              )
            ORDER BY gpu_index
        )sql";

        Statement stmt(db_, kSql, "query snapshot gpu");
        if (stmt.ok() && stmt.BindText(1, machine_id) && stmt.BindText(2, machine_id)) {
            while (stmt.StepRow()) {
                GpuSnapshotRow gpu;
                gpu.gpu_index = stmt.ColumnInt(0);
                gpu.gpu_name = stmt.ColumnText(1);
                gpu.core_c = stmt.ColumnOptionalDouble(2);
                gpu.hotspot_c = stmt.ColumnOptionalDouble(3);
                gpu.util_gpu_pct = stmt.ColumnOptionalDouble(4);
                gpu.vram_used_bytes = stmt.ColumnOptionalInt64(5);
                gpu.vram_total_bytes = stmt.ColumnOptionalInt64(6);
                gpu.power_w = stmt.ColumnOptionalDouble(7);
                snapshot.gpus.push_back(std::move(gpu));
            }
        }
    }

    // Ollama metrics from latest ollama_runtime_samples
    {
        static constexpr std::string_view kSql = R"sql(
            SELECT loaded_model_count, resident_vram_bytes, active_request_count
            FROM ollama_runtime_samples
            WHERE machine_id = ?
            ORDER BY ts_utc_ms DESC
            LIMIT 1
        )sql";

        Statement stmt(db_, kSql, "query snapshot ollama");
        if (stmt.ok() && stmt.BindText(1, machine_id)) {
            if (stmt.StepRow()) {
                snapshot.ollama_reachable = true;
                auto loaded = stmt.ColumnOptionalInt(0);
                snapshot.ollama_loaded_model_count = loaded.value_or(0);
                snapshot.ollama_resident_vram_bytes = stmt.ColumnOptionalInt64(1);
            }
        }
    }

    return snapshot;
}

}  // namespace host_agent::storage
