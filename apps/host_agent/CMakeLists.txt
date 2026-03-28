if(NOT WIN32)
    message(FATAL_ERROR "host_agent currently targets Windows only.")
endif()

# ── Sources ──────────────────────────────────────────────────────────────────
set(HOST_AGENT_SOURCES
    src/pch.cpp
    src/main.cpp
    src/core/bridge_message_parser.cpp
    src/core/service.cpp
    src/ipc/named_pipe_server_win32.cpp
    src/storage/sqlite_store.cpp
    src/api/http_server.cpp
    src/collectors/direct_gpu_collector.cpp
    src/collectors/ollama_runtime_collector.cpp
)

set(VENDORED_C_SOURCES
    ${CMAKE_SOURCE_DIR}/third_party/cjson/cJSON.c
    ${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite3.c
)

# ── Target ───────────────────────────────────────────────────────────────────
add_executable(host_agent ${HOST_AGENT_SOURCES} ${VENDORED_C_SOURCES})

target_include_directories(host_agent PRIVATE
    include
    ${CMAKE_SOURCE_DIR}/third_party/cjson
    ${CMAKE_SOURCE_DIR}/third_party/sqlite
    ${CMAKE_SOURCE_DIR}/third_party/httplib
)

# ── Definitions ──────────────────────────────────────────────────────────────
target_compile_definitions(host_agent PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    CJSON_HIDE_SYMBOLS
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    # Release-only SQLite tuning
    $<$<CONFIG:Release>:SQLITE_DQS=0>
    $<$<CONFIG:Release>:SQLITE_DEFAULT_MEMSTATUS=0>
    $<$<CONFIG:Release>:SQLITE_DEFAULT_WAL_SYNCHRONOUS=1>
    $<$<CONFIG:Release>:SQLITE_LIKE_DOESNT_MATCH_BLOBS>
    $<$<CONFIG:Release>:SQLITE_MAX_EXPR_DEPTH=0>
    $<$<CONFIG:Release>:SQLITE_USE_ALLOCA>
)

# ── Compiler warnings ───────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(host_agent PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(host_agent PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Link dependencies ───────────────────────────────────────────────────────
if(TARGET gpu_telemetry)
    target_link_libraries(host_agent PRIVATE gpu_telemetry)
endif()

# Windows system libraries
target_link_libraries(host_agent PRIVATE ws2_32 advapi32)

# ── Precompiled headers ─────────────────────────────────────────────────────
target_precompile_headers(host_agent PRIVATE src/pch.h)

set_source_files_properties(${VENDORED_C_SOURCES}
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)
