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

#include "inputosmlog.h"

#include <cstring>
#include <filesystem>

namespace input_osm
{

bool decode_metadata;
std::function<bool(span_t<node_t>)> node_handler;
std::function<bool(span_t<way_t>)> way_handler;
std::function<bool(span_t<relation_t>)> relation_handler;
mode_t osc_mode;
thread_local size_t thread_index{0};
thread_local size_t block_index{0};
file_type_t file_type{file_type_t::xml};
bool verbose = true;

bool input_pbf(const char* filename) noexcept;
bool input_xml(const char* filename);

bool input_file(const char* filename,
                bool decode_metadata,
                std::function<bool(span_t<node_t>)> node_handler,
                std::function<bool(span_t<way_t>)> way_handler,
                std::function<bool(span_t<relation_t>)> relation_handler) noexcept
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

    size_t len = strlen(filename);
    namespace fs = std::filesystem;
    const std::string extension = fs::path(filename).extension().string();
    if (extension == ".osm" || extension == ".osc")
    {
        input_osm::file_type = file_type_t::xml;
    }
    else if (extension == ".pbf")
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

void set_verbose(bool value)
{
    verbose = value;
}

} // namespace input_osm
