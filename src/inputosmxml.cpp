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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expat.h>
#include <string>
#include <vector>
#include <ctime>
#include "timeutil.h"

namespace input_osm {

extern bool decode_metadata;
extern std::function<bool(span_t<node_t>)> node_handler;
extern std::function<bool(span_t<way_t>)> way_handler;
extern std::function<bool(span_t<relation_t>)> relation_handler;

bool parser_enabled;
node_t current_node;
way_t current_way;
relation_t current_relation;

enum class current_tag_t
{
    none,
    node,
    way,
    relation
};
current_tag_t current_tag;
std::vector<std::string> current_strings;
std::vector<tag_t> current_tags;
std::vector<int64_t> current_refs;
std::vector<relation_member_t> current_members;

static void
xml_start_node(const char **attr)
{
    // node start
    current_node = node_t();
    current_tag = current_tag_t::node;
    for(int i = 0; attr[i]; i += 2)
    {
        if(strcmp(attr[i], "id") == 0)
            current_node.id = atoll(attr[i + 1]);
        if(strcmp(attr[i], "lat") == 0)
        {
            //current_node.latitude = atof(attr[i + 1]);
            double latitude = atof(attr[i + 1]);
            current_node.raw_latitude = latitude * 10000000;
        }
        if(strcmp(attr[i], "lon") == 0)
        {
            // current_node.longitude = atof(attr[i + 1]);
            double longitude = atof(attr[i + 1]);
            current_node.raw_longitude = longitude * 10000000;
        }
        if(strcmp(attr[i], "version") == 0)
            current_node.version = atoi(attr[i + 1]);
        if(strcmp(attr[i], "changeset") == 0)
            current_node.changeset = atoll(attr[i + 1]);
        if(strcmp(attr[i], "timestamp") == 0)
            current_node.timestamp = str_to_timestamp(attr[i + 1]);
    }
}

static void
xml_end_node()
{
    // node end
    current_tag = current_tag_t::none;
    current_node.tags = {current_tags.data(), current_tags.size() };
    if(parser_enabled && node_handler)
        parser_enabled = node_handler({&current_node, 1});
    current_tags.clear();
    current_strings.clear();
}

static void
xml_start_way(const char **attr)
{
    // way start
    current_way = way_t();
    current_tag = current_tag_t::way;
    for(int i = 0; attr[i]; i += 2)
    {
        if(strcmp(attr[i], "id") == 0)
            current_way.id = atoll(attr[i + 1]);
        if(strcmp(attr[i], "version") == 0)
            current_way.version = atoi(attr[i + 1]);
        if(strcmp(attr[i], "changeset") == 0)
            current_way.changeset = atoll(attr[i + 1]);
        if(strcmp(attr[i], "timestamp") == 0)
            current_way.timestamp = str_to_timestamp(attr[i + 1]);
    }
}

static void
xml_end_way()
{
    // way end
    current_tag = current_tag_t::none;
    current_way.tags = { current_tags.data(), current_tags.size()};
    current_way.node_refs = {current_refs.data(), current_refs.size()};
    if(parser_enabled && way_handler)
        parser_enabled = way_handler({&current_way, 1});
    current_tags.clear();
    current_strings.clear();
    current_refs.clear();
}

static void
xml_start_relation(const char **attr)
{
    // relation start
    current_relation = relation_t();
    current_tag = current_tag_t::relation;
    for(int i = 0; attr[i]; i += 2)
    {
        if(strcmp(attr[i], "id") == 0)
            current_relation.id = atoll(attr[i + 1]);
        if(strcmp(attr[i], "version") == 0)
            current_relation.version = atoi(attr[i + 1]);
        if(strcmp(attr[i], "changeset") == 0)
            current_relation.changeset = atoll(attr[i + 1]);
        if(strcmp(attr[i], "timestamp") == 0)
            current_relation.timestamp = str_to_timestamp(attr[i + 1]);
    }
}

static void
xml_end_relation()
{
    // end relation
    current_tag = current_tag_t::none;
    current_relation.tags = { current_tags.data(), current_tags.size() };
    current_relation.members = { current_members.data(), current_members.size() };
    if(parser_enabled && relation_handler)
        parser_enabled = relation_handler({&current_relation, 1});
    current_tags.clear();
    current_strings.clear();
    current_members.clear();
}

static void
xml_start_xtag(const char **attr)
{
    // tag start
    if(current_tag != current_tag_t::none)
    {
        size_t istart = current_strings.size();
        for(int i = 0; attr[i]; i += 2)
        {
            if(strcmp(attr[i], "k") == 0)
                current_strings.emplace_back(attr[i + 1]);
            if(strcmp (attr[i], "v") == 0)
                current_strings.emplace_back(attr[i + 1]);
        }
        for(auto i = istart; i < current_strings.size(); i += 2)
            current_tags.emplace_back(tag_t{current_strings[i].c_str(), current_strings[i + 1].c_str()});
    }
}

static void
xml_start_nd(const char **attr)
{
    // nd start
    if(current_tag == current_tag_t::way)
    {
        for(int i = 0; attr[i]; i += 2)
        {
            if(strcmp(attr[i], "ref") == 0)
                current_refs.emplace_back(atoll(attr[i + 1]));
        }
    }
}

static void
xml_start_member(const char **attr)
{
    // member start
    if(current_tag == current_tag_t::relation)
    {
        relation_member_t member;
        for(int i = 0; attr[i]; i += 2)
        {
            if(strcmp (attr[i], "ref") == 0)
                member.id = atoll(attr[i + 1]);
            if(strcmp(attr[i], "type") == 0)
            {
                if(strcmp(attr[i + 1], "node") == 0)
                    member.type = 0;
                if(strcmp(attr[i + 1], "way") == 0)
                    member.type = 2;
                if(strcmp(attr[i + 1], "relation") == 0)
                    member.type = 2;
            }
            if(strcmp (attr[i], "role") == 0)
            {
                current_strings.emplace_back(attr[i + 1]);
                member.role = current_strings.back().c_str();
            }
        }
        current_members.emplace_back(member);
    }
}

static void
xml_start_tag (void *data, const char *el, const char **attr)
{
    // XML tag start
    struct xml_params *params = (struct xml_params *) data;
    if(strcmp(el, "node") == 0)
        xml_start_node(attr);
    if(strcmp(el, "way") == 0)
        xml_start_way(attr);
    if(strcmp(el, "relation") == 0)
        xml_start_relation(attr);

    if(strcmp (el, "tag") == 0)
        xml_start_xtag (attr);
    if(strcmp (el, "nd") == 0)
        xml_start_nd(attr);
    if(strcmp (el, "member") == 0)
        xml_start_member(attr);

    if(strcmp(el,"create") == 0)
        osc_mode = mode_t::create;
    if(strcmp(el,"modify") == 0)
        osc_mode = mode_t::modify;
    if(strcmp(el,"delete") == 0)
        osc_mode = mode_t::destroy;
}

static void
xml_end_tag(void *data, const char *el)
{
    // XML tag end
    if(strcmp(el, "node") == 0)
        xml_end_node();
    if(strcmp (el, "way") == 0)
        xml_end_way();
    if(strcmp (el, "relation") == 0)
       xml_end_relation();

    if(strcmp(el,"create") == 0)
        osc_mode = mode_t::bulk;
    if(strcmp(el,"modify") == 0)
        osc_mode = mode_t::bulk;
    if(strcmp(el,"delete") == 0)
        osc_mode = mode_t::bulk;
}

bool input_xml(const char* filename)
{
    bool result = true;
    FILE *f = fopen(filename, "rb");
    if(!f)
    {
        perror(filename);
        return false;
    }
    XML_Parser parser = XML_ParserCreate(nullptr);
    if(!parser)
    {
        fclose(f);
        return false;
    }
    XML_SetElementHandler(parser, xml_start_tag, xml_end_tag);
    const size_t BUFFSIZE = 8192;
    char xml_buff[BUFFSIZE + 1];
    int done = 0;
    int len;
    parser_enabled = true;
    current_tag = current_tag_t::none;
    while(!done)
    {
        len = fread(xml_buff, 1, BUFFSIZE - 1, f);
        if(ferror(f))
        {
            result = false;
            break;
        }
        done = feof(f) || !parser_enabled;
        if(!XML_Parse(parser, xml_buff, len, done))
        {
            result = false;
            printf("Error parsing xml!\n");
            xml_buff[len] = 0;
            printf("Buffer: %s \n", xml_buff);
            break;
        }
    }
    if(parser)
    {
        XML_ParserFree(parser);
        parser = nullptr;
    }
    if(f)
    {
        fclose(f);
        f = nullptr;
    }
    return result;
}

} // namespace