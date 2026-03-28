#pragma once

// Standard library headers frequently used across the project
#include <string>
#include <chrono>
#include <filesystem>
#include <format>
#include <vector>
#include <memory>
#include <iostream>
#include <functional>

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
