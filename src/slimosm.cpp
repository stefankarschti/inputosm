#include "slimosm.h"
#include <cstring>

namespace slim_osm {

bool decode_metadata;
bool decode_node_coord;
std::function<bool(const node_t&)> node_handler;
std::function<bool(const way_t&)> way_handler;
std::function<bool(const relation_t&)> relation_handler;
mode_t osc_mode;

bool input_pbf(const char* filename);
bool input_xml(const char* filename);

bool input_file(const char* filename,
                bool decode_metadata,
                bool decode_node_coord,
                std::function<bool(const node_t&)> node_handler,
                std::function<bool(const way_t&)> way_handler,
                std::function<bool(const relation_t&)> relation_handler)
{
    slim_osm::decode_metadata = decode_metadata;
    slim_osm::decode_node_coord = decode_node_coord;
    slim_osm::node_handler = node_handler;
    slim_osm::way_handler = way_handler;
    slim_osm::relation_handler = relation_handler;
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
