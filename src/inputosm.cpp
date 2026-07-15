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

#include <inputosm/inputosm.h>

#include "inputosm/inputosm_c.h"
#include "inputosm_internal.h"
#include "inputosmlog.h"

#include <cstring>
#include <string_view>
#include <thread>

namespace input_osm
{

bool decode_metadata;
std::function<bool(const node_t*, size_t)> node_handler;
std::function<bool(const way_t*, size_t)> way_handler;
std::function<bool(const relation_t*, size_t)> relation_handler;
mode_t osc_mode;
thread_local size_t thread_index{0};
thread_local size_t block_index{0};
file_type_t file_type{file_type_t::xml};
bool verbose = true;
size_t g_thread_count = 1;

bool input_pbf(const char* filename) noexcept;
bool input_xml(const char* filename);

template <typename NodeHandler, typename WayHandler, typename RelationHandler>
bool input_file_(const char* filename,
                 bool decode_metadata,
                 NodeHandler&& node_handler,
                 WayHandler&& way_handler,
                 RelationHandler&& relation_handler) noexcept
{
    input_osm::decode_metadata = decode_metadata;
    input_osm::node_handler = std::move(node_handler);
    input_osm::way_handler = way_handler;
    input_osm::relation_handler = relation_handler;
    input_osm::osc_mode = mode_t::bulk;
    input_osm::file_type = file_type_t::xml;
    input_osm::thread_index = 0;
    input_osm::block_index = 0;
    bool result = false;

    if (!filename)
    {
        IOSM_ERROR("Invalid file name: null");
        return false;
    }

    std::string_view filename_sv = filename; // does the strlen
    size_t pos_of_period = filename_sv.find_last_of('.');
    std::string_view extension;
    if (pos_of_period != std::string_view::npos)
    {
        extension = filename_sv.substr(pos_of_period);
    }
    constexpr std::string_view k_osm = ".osm";
    constexpr std::string_view k_osc = ".osc";
    constexpr std::string_view k_pbf = ".pbf";

    if (extension.compare(k_osm) == 0 || extension.compare(k_osc) == 0)
    {
        input_osm::file_type = file_type_t::xml;
    }
    else if (extension.compare(k_pbf) == 0)
    {
        input_osm::file_type = file_type_t::pbf;
    }
    else
    {
        IOSM_ERROR("Can't detect type from: %s", filename);
        return false;
    }

    switch (input_osm::file_type)
    {
        case file_type_t::pbf:
            result = input_pbf(filename);
            break;
        case file_type_t::xml:
            result = input_xml(filename);
            break;
    };
    return result;
}

bool input_file(const char* filename,
                bool decode_metadata,
                std::function<bool(const node_t*, size_t)> node_handler,
                std::function<bool(const way_t*, size_t)> way_handler,
                std::function<bool(const relation_t*, size_t)> relation_handler) noexcept
{
    return input_file_(filename, decode_metadata, node_handler, way_handler, relation_handler);
}

void set_verbose(bool value)
{
    inputosm_set_verbose(value);
}

void set_thread_count(size_t count)
{
    inputosm_set_thread_count(count);
}

void set_max_thread_count()
{
    inputosm_set_max_thread_count();
}

size_t thread_count()
{
    return inputosm_thread_count();
}

} // namespace input_osm

extern "C" void inputosm_set_verbose(const bool verbose)
{
    input_osm::verbose = verbose;
}

extern "C" void inputosm_set_thread_count(const size_t count)
{
    const size_t hw_threads = std::max(1U, std::thread::hardware_concurrency());
    input_osm::g_thread_count = std::min(count, hw_threads);
}

extern "C" size_t inputosm_thread_count()
{
    return input_osm::g_thread_count;
}

extern "C" void inputosm_set_max_thread_count()
{
    input_osm::g_thread_count = std::max(1U, std::thread::hardware_concurrency());
}

extern "C" size_t inputosm_thread_index()
{
    return input_osm::thread_index;
}

extern "C" bool inputosm_input_file(const char* filename, bool decode_metadata, inputosm_callbacks_t handlers)
{
    return input_osm::input_file_(
        filename,
        decode_metadata,
        [handlers](const inputosm_node_t* nodes, size_t nodes_size) {
            return handlers.node_handler(handlers.user_data, nodes, nodes_size);
        },
        [handlers](const inputosm_way_t* ways, size_t ways_size) {
            return handlers.way_handler(handlers.user_data, ways, ways_size);
        },
        [handlers](const inputosm_relation_t* relations, size_t relations_size) {
            return handlers.relation_handler(handlers.user_data, relations, relations_size);
        });
}