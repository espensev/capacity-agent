cmake_minimum_required(VERSION 3.25)

project(ollama_telemetry_host LANGUAGES C CXX)

# ── Standards ────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Default build type (single-config generators like Ninja) ─────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type specified — defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE
        PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ── Link-Time Optimization for Release builds ───────────────────────────────
if(CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_OUTPUT)
    if(IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "IPO/LTO enabled for ${CMAKE_BUILD_TYPE}")
    else()
        message(STATUS "IPO/LTO not supported: ${IPO_OUTPUT}")
    endif()
else()
    message(STATUS "IPO/LTO skipped for ${CMAKE_BUILD_TYPE}")
endif()

# ── Global compiler flags ────────────────────────────────────────────────────
if(MSVC)
    # /MP   = parallel compilation across translation units
    # /utf-8 = source and execution charset
    add_compile_options(/MP /utf-8)

    # Release optimisations
    # /O2    = maximize speed
    # /Ob3   = aggressive inlining (MSVC 19.27+)
    # /GL    = whole-program optimisation (pairs with /LTCG linker flag)
    # /Gy    = function-level linking
    # /GS-   = disable buffer security checks (perf-sensitive code)
    add_compile_options(
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ob3>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Release>:/Gy>
        $<$<CONFIG:Release>:/GS->
        $<$<CONFIG:RelWithDebInfo>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/Ob2>
        $<$<CONFIG:RelWithDebInfo>:/GL>
        $<$<CONFIG:RelWithDebInfo>:/Gy>
        $<$<CONFIG:RelWithDebInfo>:/Zi>
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:RelWithDebInfo>:/LTCG>
        $<$<CONFIG:RelWithDebInfo>:/OPT:REF>
        $<$<CONFIG:RelWithDebInfo>:/OPT:ICF>
        $<$<CONFIG:RelWithDebInfo>:/DEBUG>
    )
else()
    # GCC / Clang
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:Release>:-DNDEBUG>
        $<$<CONFIG:RelWithDebInfo>:-O2>
        $<$<CONFIG:RelWithDebInfo>:-g>
        $<$<CONFIG:RelWithDebInfo>:-march=native>
        $<$<CONFIG:RelWithDebInfo>:-DNDEBUG>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-g>
    )
    add_link_options(
        $<$<CONFIG:Release>:-s>
    )
endif()

# ── Component toggles ───────────────────────────────────────────────────────
option(OLLAMA_TELEMETRY_BUILD_GPU_TELEMETRY  "Build the gpu_telemetry library"    ON)
option(OLLAMA_TELEMETRY_BUILD_HOST_AGENT     "Build the native host agent"        ON)
option(OLLAMA_TELEMETRY_BUILD_DOTNET_BRIDGES "Build the .NET bridge projects"     ON)

# ── Sub-projects ─────────────────────────────────────────────────────────────
if(OLLAMA_TELEMETRY_BUILD_GPU_TELEMETRY)
    add_subdirectory(libs/gpu_telemetry)
endif()

if(OLLAMA_TELEMETRY_BUILD_HOST_AGENT)
    add_subdirectory(apps/host_agent)
endif()

if(OLLAMA_TELEMETRY_BUILD_DOTNET_BRIDGES)
    add_subdirectory(bridges)
endif()

# ── Summary ──────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════╗")
message(STATUS "║  ollama_telemetry_host — build configuration    ║")
message(STATUS "╠══════════════════════════════════════════════════╣")
message(STATUS "║  Generator:      ${CMAKE_GENERATOR}")
message(STATUS "║  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "║  C++ standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "║  IPO/LTO:        ${CMAKE_INTERPROCEDURAL_OPTIMIZATION}")
message(STATUS "║  gpu_telemetry:  ${OLLAMA_TELEMETRY_BUILD_GPU_TELEMETRY}")
message(STATUS "║  host_agent:     ${OLLAMA_TELEMETRY_BUILD_HOST_AGENT}")
message(STATUS "║  dotnet bridges: ${OLLAMA_TELEMETRY_BUILD_DOTNET_BRIDGES}")
message(STATUS "╚══════════════════════════════════════════════════╝")
message(STATUS "")
