# ── .NET Bridge Projects ─────────────────────────────────────────────────────

find_program(DOTNET_EXECUTABLE dotnet REQUIRED)

set(DOTNET_CLI_HOME_DIR "${CMAKE_SOURCE_DIR}/.dotnet-home")
set(DOTNET_NUGET_PACKAGES_DIR "${CMAKE_SOURCE_DIR}/.nuget/packages")
file(MAKE_DIRECTORY "${DOTNET_CLI_HOME_DIR}" "${DOTNET_NUGET_PACKAGES_DIR}")

set(DOTNET_ENV
    "${CMAKE_COMMAND}" -E env
    "DOTNET_CLI_HOME=${DOTNET_CLI_HOME_DIR}"
    "DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1"
    "DOTNET_CLI_TELEMETRY_OPTOUT=1"
    "DOTNET_NOLOGO=1"
    "NUGET_PACKAGES=${DOTNET_NUGET_PACKAGES_DIR}"
)

# Map CMake build type to .NET configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(DOTNET_CONFIG "Debug")
    set(DOTNET_PUBLISH OFF)
else()
    set(DOTNET_CONFIG "Release")
    set(DOTNET_PUBLISH ON)
endif()

# ── librehw_bridge ───────────────────────────────────────────────────────────
set(LIBREHW_BRIDGE_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/librehw_bridge/librehw_bridge.csproj")
set(LIBREHW_BRIDGE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/librehw_bridge")

if(DOTNET_PUBLISH)
    # Release: use dotnet publish for a self-contained, trimmed output
    add_custom_target(librehw_bridge ALL
        COMMAND ${DOTNET_ENV} ${DOTNET_EXECUTABLE} publish
            "${LIBREHW_BRIDGE_PROJECT}"
            -c ${DOTNET_CONFIG}
            -r win-x64
            -o "${LIBREHW_BRIDGE_OUTPUT_DIR}"
            --self-contained false
            -p:PublishReadyToRun=true
            --verbosity minimal
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/librehw_bridge"
        COMMENT "Publishing .NET project: librehw_bridge (${DOTNET_CONFIG}, ReadyToRun)"
        VERBATIM
    )
else()
    # Debug: plain build for fast iteration
    add_custom_target(librehw_bridge ALL
        COMMAND ${DOTNET_ENV} ${DOTNET_EXECUTABLE} build
            "${LIBREHW_BRIDGE_PROJECT}"
            -c ${DOTNET_CONFIG}
            -o "${LIBREHW_BRIDGE_OUTPUT_DIR}"
            --verbosity minimal
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/librehw_bridge"
        COMMENT "Building .NET project: librehw_bridge (${DOTNET_CONFIG})"
        VERBATIM
    )
endif()

set_target_properties(librehw_bridge PROPERTIES FOLDER "Bridges")

# ── Install ──────────────────────────────────────────────────────────────────
install(DIRECTORY "${LIBREHW_BRIDGE_OUTPUT_DIR}/"
    DESTINATION bin/bridges/librehw_bridge
    FILES_MATCHING
    PATTERN "*.exe"
    PATTERN "*.dll"
    PATTERN "*.json"
    PATTERN "*.pdb" EXCLUDE
)

# ── Clean target ─────────────────────────────────────────────────────────────
add_custom_target(clean_dotnet
    COMMAND ${DOTNET_ENV} ${DOTNET_EXECUTABLE} clean "${LIBREHW_BRIDGE_PROJECT}" -c ${DOTNET_CONFIG}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/librehw_bridge"
    COMMENT "Cleaning .NET artifacts"
)

# ── Status ───────────────────────────────────────────────────────────────────
message(STATUS "  librehw_bridge: ${LIBREHW_BRIDGE_PROJECT}")
message(STATUS "  .NET config:   ${DOTNET_CONFIG} (publish=${DOTNET_PUBLISH})")
message(STATUS "  Output:        ${LIBREHW_BRIDGE_OUTPUT_DIR}")
