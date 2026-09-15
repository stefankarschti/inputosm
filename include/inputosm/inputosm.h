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

#ifndef INPUTOSM_H
#define INPUTOSM_H

#include "span.h"

#include <cstdint>
#include <functional>

namespace input_osm
{

struct tag_t
{
    const char* key = nullptr;
    const char* value = nullptr;
};

struct node_t
{
    int64_t id = 0;
    int64_t raw_latitude = 0;
    int64_t raw_longitude = 0;
    span_t<tag_t> tags;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};
static_assert(sizeof(node_t) <= 64);

struct way_t
{
    int64_t id = 0;
    span_t<int64_t> node_refs;
    span_t<tag_t> tags;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};
static_assert(sizeof(way_t) <= 64);

struct relation_member_t
{
    /**
     * @brief Type of the relation member.
     * @details A node has type 0. A way has type 1. A relation has type 2.
     */
    uint8_t type = 0;
    int64_t id = 0;
    const char* role = nullptr;
};

struct relation_t
{
    int64_t id = 0;
    span_t<relation_member_t> members;
    span_t<tag_t> tags;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};
static_assert(sizeof(relation_t) <= 64);

enum class file_type_t
{
    pbf,
    xml
};

enum class mode_t
{
    bulk,
    create,
    modify,
    destroy
};

void set_verbose(bool value);

bool input_file(const char* filename,
                bool decode_metadata,
                std::function<bool(span_t<node_t>)> node_handler,
                std::function<bool(span_t<way_t>)> way_handler,
                std::function<bool(span_t<relation_t>)> relation_handler) noexcept;

void set_thread_count(size_t);

void set_max_thread_count();

size_t thread_count();

enum log_level_t : uint8_t
{
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_INFO = 4,
    LOG_LEVEL_ERROR = 7,
    LOG_LEVEL_DISABLED = 255,
};

/**
 * @brief Set the log level.
 * @note This function is not thread-safe.
 */
void set_log_level(log_level_t) noexcept;

/**
 * @brief Callback that sends log messages to the user.
 * @note The message is a C string that ends with \0.
 */
using log_callback_t = void (*)(log_level_t level, const char* message);

/**
 * @brief Set the log callback.
 * @param log_callback The new log callback.
 * @note Multiple threads can call the callback at the same time. The callback must be thread-safe.
 * @return true if the function sets the callback. A null callback causes the function to return false.
 */
bool set_log_callback(log_callback_t log_callback) noexcept;

extern thread_local size_t thread_index;
extern thread_local size_t block_index;
extern mode_t osc_mode;
extern file_type_t file_type;

} // namespace input_osm

#endif // !INPUTOSM_H
