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

#include "inputosm_c.h"

#include <cstddef>
#include <functional>

namespace input_osm
{

using tag_t = inputosm_tag_t;
using relation_member_t = inputosm_relation_member_t;
using log_level_t = inputosm_log_level_t;
using log_callback_t = inputosm_log_callback_t;

using node_t = inputosm_node_t;
static_assert(sizeof(node_t) <= 64);

using way_t = inputosm_way_t;
static_assert(sizeof(way_t) <= 64);

using relation_t = inputosm_relation_t;
static_assert(sizeof(relation_t) <= 64);

enum class file_type_t
{
    pbf = INPUTOSM_PBF,
    xml = INPUTOSM_XML
};

enum class mode_t
{
    bulk = INPUTOSM_BULK,
    create = INPUTOSM_CREATE,
    modify = INPUTOSM_MODIFY,
    destroy = INPUTOSM_DESTROY
};

void set_verbose(bool value);

bool input_file(const char* filename,
                bool decode_metadata,
                std::function<bool(const node_t*, size_t)> node_handler,
                std::function<bool(const way_t*, size_t)> way_handler,
                std::function<bool(const relation_t*, size_t)> relation_handler) noexcept;

void set_thread_count(size_t);

void set_max_thread_count();

size_t thread_count();

/**
 * @brief Set log level
 * @note not thread safe
 */
void set_log_level(log_level_t log_level) noexcept;

/**
 * @brief Set the log callback
 * @param log_callback new log callback
 * @note the log callback will be called from multiple threads, it should be thread safe
 * @return true if set was OK
 */
bool set_log_callback(log_callback_t log_callback) noexcept;

extern thread_local size_t thread_index;
extern thread_local size_t block_index;
extern mode_t osc_mode;
extern file_type_t file_type;

} // namespace input_osm

#endif // !INPUTOSM_H
