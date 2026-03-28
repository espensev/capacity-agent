# Third-Party Sources

Only dependencies used by the current product live here.

- `cjson/`
  Vendored `cJSON` sources consumed by `apps/host_agent`.
- `sqlite/`
  Vendored SQLite amalgamation consumed by `apps/host_agent`.

- `httplib/`
  Vendored `cpp-httplib` v0.18.3 (header-only HTTP server/client).
  Used by `apps/host_agent` for the localhost read API and Ollama polling.
  Source: https://github.com/yhirose/cpp-httplib (MIT License).

These files were promoted out of the earlier source-material tree so the root layout reflects actual dependencies instead of mixed examples.
