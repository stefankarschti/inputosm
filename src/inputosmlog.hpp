// Copyright 2021-2026 Stefan Karschti
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <inputosm/inputosm.hpp>
#include <fmt/format.h>
#include <utility>

#define INPUT_OSM_LOG_ENABLED 1

namespace input_osm
{

extern log_level_t g_log_level;
extern log_callback_t g_log_callback;

/**
 * @brief Write a log message with the specified level and format.
 * @param level The log level.
 * @param format The format string. The compiler checks this string.
 * @param args The arguments for the format string.
 */
template <typename... T>
void log(log_level_t level, fmt::format_string<T...> format, T&&... args) noexcept
{
    if (level < g_log_level) return;

    char buffer[512];
    const auto result = fmt::format_to_n(buffer, sizeof(buffer) - 1, format, std::forward<T>(args)...);
    *result.out = '\0';
    g_log_callback(level, buffer);
}

} // namespace input_osm

#ifdef INPUT_OSM_LOG_ENABLED
#define IOSM_TRACE(fmt, ...) log(input_osm::LOG_LEVEL_TRACE, fmt __VA_OPT__(, ) __VA_ARGS__)
#define IOSM_INFO(fmt, ...) log(input_osm::LOG_LEVEL_INFO, fmt __VA_OPT__(, ) __VA_ARGS__)
#define IOSM_ERROR(fmt, ...) log(input_osm::LOG_LEVEL_ERROR, fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define IOSM_TRACE(fmt, ...)
#define IOSM_INFO(fmt, ...)
#define IOSM_ERROR(fmt, ...)
#endif
