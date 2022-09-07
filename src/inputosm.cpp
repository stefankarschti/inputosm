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

#include <cstring>

namespace input_osm {

bool decode_metadata;
bool decode_node_coord;
std::function<bool(const node_t&)> node_handler;
std::function<bool(const way_t&)> way_handler;
std::function<bool(const relation_t&)> relation_handler;
mode_t osc_mode;
thread_local size_t thread_index{0};

bool input_pbf(const char* filename);
bool input_xml(const char* filename);

bool input_file(const char* filename,
                bool decode_metadata,
                bool decode_node_coord,
                std::function<bool(const node_t&)> node_handler,
                std::function<bool(const way_t&)> way_handler,
                std::function<bool(const relation_t&)> relation_handler)
{
    input_osm::decode_metadata = std::move(decode_metadata);
    input_osm::decode_node_coord = std::move(decode_node_coord);
    input_osm::node_handler = std::move(node_handler);
    input_osm::way_handler = way_handler;
    input_osm::relation_handler = relation_handler;
    osc_mode = mode_t::bulk;
    bool result = false;

    if(!filename)
	    return false;

    size_t len = strlen(filename);
    file_type_t type;
    if(len > 4 && strcasecmp(filename + len - 4, ".osm") == 0)
    {
    	type = file_type_t::xml;
    }
    else if (len > 4 && strcasecmp(filename + len - 4, ".pbf") == 0)
    {	
    	type = file_type_t::pbf;
    }
    else if (len > 4 && strcasecmp(filename + len - 4, ".osc") == 0)
    {   
    	type = file_type_t::xml;
    }
    else
	    return false;

    switch(type) 
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

} // namespace slim_osm
