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

#include <cstdint>
#include <functional>

namespace input_osm {
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
    double longitude = 0;
    double latitude = 0;
    size_t tag_count = 0;
    tag_t *tags = nullptr;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};

struct way_t
{
    int64_t id = 0;
    size_t node_ref_count = 0;
    int64_t *node_refs = nullptr;
    size_t tag_count = 0;
    tag_t *tags = nullptr;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};

struct relation_member_t
{
    /**
     * @brief Relation type
     * @details NODE = 0; WAY = 1; RELATION = 2;
     */
    uint8_t type = 0;
    int64_t id = 0;
    const char* role = nullptr;
};

struct relation_t
{
    int64_t id = 0;
    size_t member_count = 0;
    relation_member_t *members = nullptr;
    size_t tag_count = 0;
    tag_t *tags = nullptr;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};

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

extern mode_t osc_mode;

bool input_file(const char* filename,
                bool decode_metadata,
                bool decode_node_coord,
                std::function<bool(const node_t&)> node_handler,
                std::function<bool(const way_t&)> way_handler,
                std::function<bool(const relation_t&)> relation_handler);

} // namespace

#endif // !INPUTOSM_H
