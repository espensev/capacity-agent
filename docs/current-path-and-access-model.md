# Current Path And Access Model

## Purpose

This note summarizes:

- the current implemented telemetry path
- what has been verified end to end
- what is still missing
- how `maindesk` should access the data
- which access model best matches the intended architecture

The key design question is not whether this machine should "send data out" directly. The intended boundary is that each monitored machine owns its own local collection and persistence, and remote consumers talk to the native host boundary rather than to raw collectors.

## Intended Boundary

The architecture already points in a consistent direction:

- `librehw_bridge` is a local sensor sidecar only
- `host_agent` owns persistence, policy, and future read/control boundaries
- remote access should happen through the native host boundary, not through the bridge and not through direct SQLite file access

That means the remote machine should look like this:

```text
LibreHardwareMonitorLib
        |
        v
librehw_bridge
        |
        | named pipe (NDJSON)
        v
host_agent
        |
        +--> local SQLite
        |
        +--> localhost read API (future)
```

And `maindesk` should access it like this:

```text
maindesk
   |
   +--> SSH tunnel or authenticated proxy
             |
             v
      host_agent localhost API on the remote machine
```

## Current Implemented Path

Today the working path is:

1. `librehw_bridge` loads `LibreHardwareMonitorLib`.
2. The bridge emits UTF-8 NDJSON over the named pipe.
3. `host_agent` receives `hello`, `catalog_snapshot`, `sample_batch`, and `heartbeat`.
4. `host_agent` persists those messages into local SQLite.

The current persisted tables in active use are:

- `machine_catalog`
- `collector_health`
- `sensor_catalog`
- `sensor_samples`

The higher-level tables already exist in schema but are not populated yet:

- `system_samples`
- `gpu_samples`
- `ollama_runtime_samples`
- `ollama_requests`
- `control_events`

## Verified State

The following has now been verified locally:

- `librehw_bridge` builds successfully.
- `host_agent` builds successfully.
- The bridge emits a consistent source key: `librehw_bridge`.
- The native host accepts bridge traffic and writes to SQLite.
- End-to-end smoke test succeeded with both processes running locally against a temporary database.

Smoke test result:

- `machine_catalog = 1`
- `collector_health = 1`
- `sensor_catalog = 263`
- `sensor_samples = 1052`
- all persisted source values were `librehw_bridge`
- host stderr was empty
- bridge stderr was empty

That means the current system is already a working local ingestion path:

```text
collector -> host_agent -> local SQLite
```

This is materially better than scraping a webserver surface because the system now preserves stable sensor identity at the library/API boundary.

## What Is Still Missing

The current implementation is not yet a complete readiness-state service.

Missing pieces:

- no localhost read API yet
- no remote access boundary implemented yet
- no derived readiness reducer yet
- no population of `system_samples`
- no population of `gpu_samples`
- no Ollama runtime/request collector yet
- no authenticated control/read boundary for `maindesk`

So the current state is:

- local raw telemetry ingestion: working
- local persistence: working
- remote read path for `maindesk`: not implemented
- readiness-state output: not implemented

## Access Model Options For Maindesk

### Option A: Localhost API plus SSH tunnel

Shape:

```text
maindesk --SSH tunnel--> remote host 127.0.0.1:PORT -> host_agent API
```

Pros:

- strongest alignment with current intent
- no public service exposure required
- no direct DB file access
- simple trust model if SSH already exists
- easy to operate for a small number of machines

Cons:

- tunnel/session management lives outside the app unless later automated

Assessment:

- best near-term option
- best default option unless there is a strong reason to avoid SSH

### Option B: Localhost API plus authenticated reverse proxy

Shape:

```text
maindesk --> TLS/auth reverse proxy --> host_agent localhost API
```

Pros:

- cleaner for always-on dashboards
- simpler for many remote nodes if centrally routed

Cons:

- more moving parts
- higher security burden
- unnecessary complexity before the read API and readiness model are stable

Assessment:

- valid later
- not the best first boundary

### Option C: Push summarized state to maindesk

Shape:

```text
remote host_agent --> central ingest on maindesk
```

Pros:

- good for central history and fleet-wide views
- useful when polling many nodes becomes awkward

Cons:

- duplicates storage concerns
- introduces delivery/retry semantics
- pushes architecture toward a multi-node control plane before the local host contract is stable

Assessment:

- useful as a second phase
- should not replace the local host boundary

## What Should Not Be The Boundary

The following are not good access models:

- `maindesk` reading the SQLite database over SMB or a network share
- `maindesk` talking directly to `librehw_bridge`
- exposing the raw named pipe contract beyond the local machine
- exposing raw collection endpoints directly to the LAN or internet

Why not:

- SQLite is correct as a local persistence layer, not as a remote file protocol
- the bridge should remain a local collector, not a public API
- the named pipe contract is an internal ingest contract, not a remote client contract
- exposing raw ingest surfaces makes the security and compatibility story worse

## Recommended Best Path

The best path that aligns with current intent is:

1. Keep `librehw_bridge` local-only.
2. Keep `host_agent` as the only owned machine boundary.
3. Add a localhost-only read API to `host_agent`.
4. Have `maindesk` read that API through SSH tunneling first.
5. Add a reverse proxy only if operations later justify it.
6. Optionally add central replication after the local read contract is stable.

That gives this target model:

```text
remote machine
  librehw_bridge -> host_agent -> local SQLite -> localhost API

maindesk
  -> tunnel/proxy
  -> host_agent read API
```

## What The Host API Should Return

`maindesk` should not reconstruct readiness from raw sensor rows if `host_agent` can compute that once locally.

The host should eventually provide:

- `/v1/status`
- `/v1/snapshot`
- `/v1/history`
- `/v1/catalog`
- `/v1/readiness`

Minimal `snapshot` responsibilities:

- latest normalized machine metrics
- latest GPU metrics
- latest Ollama runtime metrics
- collector freshness
- derived readiness state
- reasons for degraded or blocked status

That keeps decision logic on the remote machine where the data is freshest and where collector-specific knowledge already lives.

## Why This Aligns With Intent

This matches the apparent intent better than either the old LHM web scraping path or a direct-DB remote reader.

Why:

- the monitored machine owns collection
- the monitored machine owns persistence
- the monitored machine owns readiness derivation
- `maindesk` consumes a stable host-level contract
- collector implementation details stay local and replaceable

This is the cleanest separation between:

- collection
- storage
- decision logic
- remote consumption

## Recommended Next Steps

1. Add a localhost-only HTTP read API to `host_agent`.
2. Populate `system_samples` from normalized bridge sensors.
3. Populate `gpu_samples` from `libs/gpu_telemetry`.
4. Add Ollama runtime and request collectors.
5. Implement a first readiness reducer in `host_agent`.
6. Make `maindesk` read `/v1/status` and `/v1/snapshot` through SSH tunneling.

## Decision Summary

Current path:

- good and now working for local ingestion into the machine's own host process and SQLite

Best access model for `maindesk`:

- read from `host_agent`, not from the bridge and not from the SQLite file
- use a localhost API on the remote machine
- access it through SSH tunneling first

This is the simplest model that aligns with the current architecture and leaves room to grow into a stronger remote-control/read surface later without throwing away the local design.
