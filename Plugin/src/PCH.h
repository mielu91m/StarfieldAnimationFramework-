#pragma once

// force_undef.h jest już wymuszony przez xmake, więc NIE powtarzamy #undef tutaj

// ============================================================================
// SEKCJA 1: STL (PRZED CommonLibSF!)
// ============================================================================
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <climits>
#include <clocale>
#include <cmath>
#include <compare>
#include <concepts>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <execution>
#include <filesystem>
#include <format>
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <iostream>
#include <istream>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <ostream>
#include <queue>
#include <random>
#include <ranges>
#include <ratio>
#include <regex>
#include <scoped_allocator>
#include <set>
#include <shared_mutex>
#include <source_location>
#include <span>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// ============================================================================
// SEKCJA 2: COMMONLIBSF / SFSE
// ============================================================================
#include <SFSE/SFSE.h>
#include <RE/Starfield.h>
#include <REL/Relocation.h>

// ============================================================================
// SEKCJA 3: SPDLOG
// ============================================================================
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#define SAF_LOG_INFO(...)  spdlog::info(__VA_ARGS__)
#define SAF_LOG_WARN(...)  spdlog::warn(__VA_ARGS__)
#define SAF_LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define SAF_LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define SAF_LOG_TRACE(...) spdlog::trace(__VA_ARGS__)

// ============================================================================
// SEKCJA 4: UTIL HELPERS - Guarded/VFuncHook defined in Util/General.h
// ============================================================================

// ============================================================================
// SEKCJA 5: ALIASY I BEZPIECZNE STAŁE
// ============================================================================
namespace logger = SFSE::log;
using namespace std::literals;

namespace SafeConstants {
    constexpr float NEAR_PLANE = 0.1f;
    constexpr float FAR_PLANE = 1000.0f;
}

#define DLLEXPORT extern "C" [[maybe_unused]] __declspec(dllexport)

// ============================================================================
// SEKCJA 6: PLUGIN INFO (definicja w main.cpp; SFSE wymaga extern "C" + export)
// ============================================================================
extern "C" DLLEXPORT extern const SFSE::PluginVersionData SFSEPlugin_Version;

// ============================================================================
// SEKCJA 7: BIBLIOTEKI ANIMACJI
// ============================================================================
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/transform.h"
#include "ozz/base/memory/unique_ptr.h"

#include "fastgltf/core.hpp"
#include "fastgltf/types.hpp"
