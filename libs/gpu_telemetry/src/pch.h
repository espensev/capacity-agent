#pragma once

// Standard library headers frequently used in gpu_telemetry
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <cstdint>

// Windows headers
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
