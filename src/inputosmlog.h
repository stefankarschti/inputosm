// Copyright 2021-2022 Stefan Karschti
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

#ifndef _INPUTOSMLOG_H_
#define _INPUTOSMLOG_H_

#include <inputosm/inputosm.h>

#define INPUT_OSM_LOG_ENABLED 1

namespace input_osm {

extern log_level_t g_log_level;
extern log_callback_t g_log_callback;

/**
 * @brief Log a message with level and printf style formatting
 * @param level log level
 * @param fmt printf style formatting string (not checked!)
 * @param va_list of args for printf
 */
void log(log_level_t level, const char* fmt, ...) noexcept;

} // namespace input_osm

#ifdef INPUT_OSM_LOG_ENABLED
#define IOSM_TRACE(fmt, ...) log(input_osm::LOG_LEVEL_TRACE, fmt __VA_OPT__(,) __VA_ARGS__)
#define IOSM_INFO(fmt, ...) log(input_osm::LOG_LEVEL_INFO, fmt __VA_OPT__(,) __VA_ARGS__)
#define IOSM_ERROR(fmt, ...) log(input_osm::LOG_LEVEL_ERROR, fmt __VA_OPT__(,) __VA_ARGS__)
#else
#define IOSM_TRACE(fmt, ...)
#define IOSM_INFO(fmt, ...)
#define IOSM_ERROR(fmt, ...)
#endif

#endif // _INPUTOSMLOG_H_
