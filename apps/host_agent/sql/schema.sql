PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA foreign_keys = ON;
PRAGMA temp_store = MEMORY;

CREATE TABLE IF NOT EXISTS metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

INSERT OR IGNORE INTO metadata (key, value) VALUES
    ('schema_name', 'host_agent'),
    ('schema_version', '1');

CREATE TABLE IF NOT EXISTS machine_catalog (
    machine_id TEXT PRIMARY KEY,
    hostname TEXT,
    display_name TEXT,
    first_seen_utc_ms INTEGER NOT NULL,
    last_seen_utc_ms INTEGER NOT NULL,
    extra_json TEXT
);

CREATE TABLE IF NOT EXISTS collector_health (
    collector_id TEXT PRIMARY KEY,
    machine_id TEXT NOT NULL,
    source TEXT NOT NULL,
    collector_type TEXT NOT NULL,
    status TEXT NOT NULL,
    last_hello_utc_ms INTEGER,
    last_sample_utc_ms INTEGER,
    last_error TEXT,
    updated_utc_ms INTEGER NOT NULL,
    extra_json TEXT
);

CREATE INDEX IF NOT EXISTS idx_collector_health_machine
    ON collector_health(machine_id, source);

CREATE TABLE IF NOT EXISTS sensor_catalog (
    sensor_uid TEXT PRIMARY KEY,
    machine_id TEXT NOT NULL,
    source TEXT NOT NULL,
    hardware_uid TEXT NOT NULL,
    hardware_path TEXT NOT NULL,
    hardware_name TEXT NOT NULL,
    hardware_type TEXT NOT NULL,
    sensor_path TEXT NOT NULL,
    sensor_name TEXT NOT NULL,
    sensor_type TEXT NOT NULL,
    sensor_index INTEGER,
    unit TEXT,
    is_default_hidden INTEGER NOT NULL DEFAULT 0,
    first_seen_utc_ms INTEGER NOT NULL,
    last_seen_utc_ms INTEGER NOT NULL,
    properties_json TEXT
);

CREATE INDEX IF NOT EXISTS idx_sensor_catalog_machine_source
    ON sensor_catalog(machine_id, source, hardware_type, sensor_type);

CREATE TABLE IF NOT EXISTS sensor_samples (
    sensor_uid TEXT NOT NULL,
    ts_utc_ms INTEGER NOT NULL,
    value REAL NOT NULL,
    min_value REAL,
    max_value REAL,
    quality TEXT NOT NULL DEFAULT 'ok',
    source TEXT NOT NULL,
    extra_json TEXT,
    PRIMARY KEY (sensor_uid, ts_utc_ms),
    FOREIGN KEY (sensor_uid) REFERENCES sensor_catalog(sensor_uid)
);

CREATE INDEX IF NOT EXISTS idx_sensor_samples_time
    ON sensor_samples(ts_utc_ms);

CREATE TABLE IF NOT EXISTS system_samples (
    machine_id TEXT NOT NULL,
    ts_utc_ms INTEGER NOT NULL,
    cpu_package_c REAL,
    cpu_total_load_pct REAL,
    cpu_clock_mhz REAL,
    memory_used_bytes INTEGER,
    memory_total_bytes INTEGER,
    motherboard_c REAL,
    disk_read_bytes_per_s INTEGER,
    disk_write_bytes_per_s INTEGER,
    disk_busy_pct REAL,
    net_rx_bytes_per_s INTEGER,
    net_tx_bytes_per_s INTEGER,
    extra_json TEXT,
    PRIMARY KEY (machine_id, ts_utc_ms)
);

CREATE INDEX IF NOT EXISTS idx_system_samples_time
    ON system_samples(ts_utc_ms);

CREATE TABLE IF NOT EXISTS gpu_samples (
    machine_id TEXT NOT NULL,
    gpu_uid TEXT NOT NULL,
    gpu_index INTEGER NOT NULL,
    ts_utc_ms INTEGER NOT NULL,
    gpu_name TEXT,
    backend TEXT NOT NULL,
    core_c REAL,
    hotspot_c REAL,
    mem_junction_c REAL,
    util_gpu_pct REAL,
    util_mem_pct REAL,
    power_w REAL,
    vram_used_bytes INTEGER,
    vram_total_bytes INTEGER,
    clock_graphics_mhz INTEGER,
    clock_memory_mhz INTEGER,
    pcie_tx_bytes_per_s INTEGER,
    pcie_rx_bytes_per_s INTEGER,
    fan0_rpm INTEGER,
    fan0_pct REAL,
    extra_json TEXT,
    PRIMARY KEY (machine_id, gpu_uid, ts_utc_ms)
);

CREATE INDEX IF NOT EXISTS idx_gpu_samples_time
    ON gpu_samples(ts_utc_ms);

CREATE TABLE IF NOT EXISTS ollama_runtime_samples (
    machine_id TEXT NOT NULL,
    ts_utc_ms INTEGER NOT NULL,
    base_url TEXT NOT NULL,
    active_request_count INTEGER,
    queued_request_count INTEGER,
    loaded_model_count INTEGER,
    resident_vram_bytes INTEGER,
    resident_ram_bytes INTEGER,
    extra_json TEXT,
    PRIMARY KEY (machine_id, ts_utc_ms)
);

CREATE INDEX IF NOT EXISTS idx_ollama_runtime_time
    ON ollama_runtime_samples(ts_utc_ms);

CREATE TABLE IF NOT EXISTS ollama_requests (
    request_id TEXT PRIMARY KEY,
    machine_id TEXT NOT NULL,
    model TEXT NOT NULL,
    status TEXT NOT NULL,
    started_utc_ms INTEGER NOT NULL,
    finished_utc_ms INTEGER,
    total_duration_ms INTEGER,
    prompt_eval_duration_ms INTEGER,
    eval_duration_ms INTEGER,
    prompt_tokens INTEGER,
    completion_tokens INTEGER,
    total_tokens INTEGER,
    tokens_per_sec REAL,
    remote_addr TEXT,
    client_name TEXT,
    error_text TEXT,
    request_json TEXT,
    response_json TEXT
);

CREATE INDEX IF NOT EXISTS idx_ollama_requests_time
    ON ollama_requests(started_utc_ms, model);

CREATE TABLE IF NOT EXISTS control_events (
    event_id TEXT PRIMARY KEY,
    machine_id TEXT NOT NULL,
    ts_utc_ms INTEGER NOT NULL,
    actor TEXT NOT NULL,
    action_type TEXT NOT NULL,
    target_kind TEXT NOT NULL,
    target_id TEXT,
    result TEXT NOT NULL,
    decision_reason TEXT,
    before_json TEXT,
    after_json TEXT,
    error_text TEXT
);

CREATE INDEX IF NOT EXISTS idx_control_events_time
    ON control_events(ts_utc_ms, action_type);

CREATE VIEW IF NOT EXISTS latest_sensor_samples AS
SELECT s.*
FROM sensor_samples s
JOIN (
    SELECT sensor_uid, MAX(ts_utc_ms) AS max_ts_utc_ms
    FROM sensor_samples
    GROUP BY sensor_uid
) latest
ON latest.sensor_uid = s.sensor_uid
AND latest.max_ts_utc_ms = s.ts_utc_ms;

