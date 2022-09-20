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
#include <cstdio>
#include <functional>
#include <vector>
#include <zlib.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <memory>
#include <queue>
#include <algorithm>
#include <iomanip>

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

static constexpr uint32_t ID5WT3(uint32_t id, uint8_t wt)
{
    constexpr uint8_t kBitsForWT = 3u;
    constexpr uint8_t kMaskForWT = ~(0xFFu << kBitsForWT) & 0xFFu;
    return (id << kBitsForWT) | (wt & kMaskForWT);
}

namespace input_osm {

/**
 * @brief 
 * @link https://wiki.openstreetmap.org/wiki/PBF_Format @endlink
 * @link https://developers.google.com/protocol-buffers/docs/encoding#structure @endlink
 */

extern bool decode_metadata, decode_node_coord;
extern std::function<bool(span_t<node_t>)> node_handler;
extern std::function<bool(span_t<way_t>)> way_handler;
extern std::function<bool(span_t<relation_t>)> relation_handler;

static constexpr bool verbose = true;
struct field_t
{
    uint32_t id5wt3; // https://developers.google.com/protocol-buffers/docs/encoding#structure
    uint8_t *pointer;
    uint64_t length;
    uint64_t value_uint64;
};

struct string_table_t
{
    std::vector<uint32_t> st_index;
    std::vector<uint8_t> st_buffer;

    void clear()
    {
        st_buffer.clear();
        st_index.clear();
    }
    void init(size_t byte_size)
    {
        st_buffer.clear();
        st_index.clear();
        if(byte_size > st_buffer.capacity())
            st_buffer.reserve(byte_size);
    }
    void add(uint8_t* buf, size_t len)
    {
        st_index.emplace_back(st_buffer.size());
        st_buffer.insert(st_buffer.end(), buf, buf + len);
        st_buffer.emplace_back(0);
    }

    const char* get(uint32_t index)
    {
        return (const char*)st_buffer.data() + st_index[index];
    }
};

// This primitive block's data
thread_local string_table_t string_table;
thread_local int32_t granularity = 100;
thread_local int64_t lat_offset = 0;
thread_local int64_t lon_offset = 0;
thread_local int32_t date_granularity = 1000;


inline uint32_t read_net_uint32(uint8_t *buf) noexcept
{
    return ((uint32_t)(buf[0]) << 24u) | ((uint32_t)(buf[1]) << 16u) | ((uint32_t)(buf[2]) << 8u) | ((uint32_t)(buf[3]));
}

inline uint64_t read_varint_uint64(uint8_t *&ptr) noexcept
{
    uint64_t v64 = 0;
    unsigned shift = 0;
    while(1)
    {
        uint64_t c = *ptr++;
        v64 |= (c & 0x7f) << shift;
        if(!(c & 0x80))
            break;
        shift += 7;
    }
    return v64;
}

inline int64_t to_sint64(uint64_t v64) noexcept
{
    return (v64 & 1) ? -(int64_t)((v64 + 1) / 2) : (v64 + 1) / 2;
}

inline uint64_t read_varint_sint64(uint8_t *&ptr) noexcept
{
    return to_sint64(read_varint_uint64(ptr));
}

inline int64_t read_varint_int64(uint8_t *&ptr) noexcept
{
    return (int64_t)read_varint_uint64(ptr);
}

inline uint8_t* read_field(uint8_t *ptr, field_t &field) noexcept
{
    field.id5wt3 = read_varint_uint64(ptr); // BUGFIX: id5wt3 is actually a varint
    field.pointer = ptr;
    switch (field.id5wt3 & 0x07) // wt
    {
    case 0: // varint
        field.value_uint64 = read_varint_uint64(ptr);
        field.length = ptr - field.pointer;
        break;
    case 1: // 64-bit
        field.length = 8;
        ptr += field.length;
        break;
    case 2: // length-delimited
        field.length = read_varint_uint64(ptr);
        field.pointer = ptr;
        ptr += field.length;
        break;
    case 5: // 32-bit
        field.length = 4;
        ptr += field.length;
        break;
    default:
        field.length = 0;
        ptr = nullptr;
    }
    return ptr;
}

inline bool unzip_compressed_block(uint8_t *zip_ptr, size_t zip_sz, uint8_t *raw_ptr, size_t raw_sz) noexcept
{
    uLongf size = raw_sz;
    int ret = uncompress(raw_ptr, &size, zip_ptr, zip_sz);
    return ret == Z_OK && size == raw_sz;
}

inline void read_sint64_packed(std::vector<int64_t>& packed, uint8_t* ptr, uint8_t* end) noexcept
{
    while(ptr < end)
        packed.emplace_back(read_varint_sint64(ptr));
}

inline void read_sint32_packed(std::vector<int32_t>& packed, uint8_t* ptr, uint8_t* end) noexcept
{
    while(ptr < end)
        packed.emplace_back(read_varint_sint64(ptr));
}

inline void read_uint32_packed(std::vector<uint32_t>& packed, uint8_t* ptr, uint8_t* end) noexcept
{
    while(ptr < end)
        packed.emplace_back(read_varint_uint64(ptr));
}

template <typename Handler>
inline bool iterate_fields(uint8_t* ptr, uint8_t* end, Handler&& handler) noexcept
{
    while(ptr < end)
    {
        field_t field;
        ptr = read_field(ptr, field);
        if(!ptr)
            return false;
        if(!handler(field))
            return false;
    }
    return true;
}

inline bool read_string_table(uint8_t* ptr, uint8_t* end) noexcept
{
    return iterate_fields(ptr, end, [&](field_t& field)->bool{
        if(field.id5wt3 == ID5WT3(1, 2)) // string
            string_table.add(field.pointer, field.length);
        return true;
    }); 
}

template <typename T>
inline bool check_capacity(std::vector<T> &vec, int index, const char* subject)
{
    size_t previous_capacity = vec.capacity();
    while(index >= vec.size())
    {
        vec.emplace_back();
        if(vec.capacity() > previous_capacity)
        {
            if(verbose)
                printf("%s capacity exceeded: %zu/%zu on thread %zu\n", subject, vec.capacity(), previous_capacity, thread_index);
            return false;
        }
    }
    return true;
}

bool read_dense_nodes(uint8_t* ptr, uint8_t* end) noexcept
{
    thread_local std::vector<node_t> node_list(16000);
    node_list.clear();
    thread_local std::vector<tag_t> tags(256000);
    tags.clear();

    if(!iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,2): // node ids. delta encoded
            {
                int64_t id = 0;
                for(auto ptr = field.pointer; ptr < field.pointer + field.length;)
                {
                    id += read_varint_sint64(ptr);
                    node_list.emplace_back();
                    node_list.back().id = id;
                }
            }
            break;
        case ID5WT3(5,2): // dense infos
            if(decode_metadata)
            {
                iterate_fields(field.pointer, field.pointer + field.length, [](field_t& field)->bool{
                    switch(field.id5wt3)
                    {
                    case ID5WT3(1,2): // versions. not delta encoded
                        {
                            auto inode = node_list.begin();
                            for(auto ptr = field.pointer; ptr < field.pointer + field.length && inode < node_list.end(); inode++)
                            {
                                inode->version = read_varint_uint64(ptr);
                            }
                        }
                        break;
                    case ID5WT3(2,2): // timestamps. delta encoded
                        {
                            int64_t timestamp = 0;
                            auto inode = node_list.begin();
                            for(auto ptr = field.pointer; ptr < field.pointer + field.length && inode < node_list.end(); inode++)
                            {
                                timestamp += read_varint_sint64(ptr);
                                inode->timestamp = timestamp;
                            }
                        }
                        break;
                    case ID5WT3(3,2): // changesets. delta encoded
                        {
                            int64_t changeset = 0;
                            auto inode = node_list.begin();
                            for(auto ptr = field.pointer; ptr < field.pointer + field.length && inode < node_list.end(); inode++)
                            {
                                changeset += read_varint_sint64(ptr);
                                inode->changeset = changeset;
                            }
                        }
                        break;
                    }
                    return true;
                });
            }
            break;
        case ID5WT3(8,2): // latitudes. delta encoded
            {
                int64_t latitude = 0;
                auto inode = node_list.begin();
                for(auto ptr = field.pointer; ptr < field.pointer + field.length && inode < node_list.end(); inode++)
                {
                    latitude += read_varint_sint64(ptr);
                    inode->raw_latitude = latitude;
                }
            }
            break;
        case ID5WT3(9,2): // longitudes. delta encoded
            {
                int64_t longitude = 0;
                auto inode = node_list.begin();
                for(auto ptr = field.pointer; ptr < field.pointer + field.length && inode < node_list.end(); inode++)
                {
                    longitude += read_varint_sint64(ptr);
                    inode->raw_longitude = longitude;
                }
            }
            break;
        case ID5WT3(10,2): // packed indexes to keys & values
            {
                bool invalid = true;
                while(invalid)
                {
                    invalid = false;
                    tags.clear();
                    auto itag_start = tags.begin();
                    auto inode = node_list.begin();
                    size_t previous_capacity = tags.capacity();
                    size_t tags_size = tags.size();
                    for(auto ptr = field.pointer; ptr < field.pointer + field.length;)
                    {
                        // read key
                        uint32_t istring = read_varint_uint64(ptr);
                        if(!istring)
                        {
                            // finish up current node
                            if(itag_start != tags.end())
                            {
                                inode->tags = span_t{&(*itag_start), static_cast<size_t>(tags.end() - itag_start)};
                            }
                            itag_start = tags.end();
                            ++inode;
                            continue;
                        }
                        // add to tags
                        tags.emplace_back();
                        ++tags_size;
                        // get key
                        tags.back().key = string_table.get(istring);
                        // read value
                        tags.back().value = string_table.get(read_varint_uint64(ptr));
                        // check for invalidity
                        if(tags_size > previous_capacity)
                        {
                            // all references to tags are invalid. restart.
                            invalid = true;
                            break;
                        }
                    }
                }
            }
            break;
        }
        return true;
    }))
        return false;

    // report nodes
    if(!node_handler(span_t{node_list.data(), node_list.size()}))
        return false;
    return true;;
}

template <class T>
bool read_info(T &obj, uint8_t* ptr, uint8_t* end) noexcept
{
    return iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,0): // version
            obj.version = field.value_uint64;
            break;
        case ID5WT3(2,0): // timestamp
            obj.timestamp = field.value_uint64;
            break;
        case ID5WT3(3,0): // changeset
            obj.changeset = field.value_uint64;
            break;
        }
        return true;
    });
}

enum class result_t
{
    ok = 0,
    error = 1,
    eoutofmem = 2
};

result_t read_way(uint8_t* ptr, uint8_t* end, std::vector<way_t>& way_list, std::vector<tag_t>& tags, std::vector<int64_t>& node_refs) noexcept
{
    way_t way;
    auto node_ref_begin = node_refs.size();
    auto tags_begin = tags.size();
    result_t result = result_t::ok;

    if(!iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,0): // way id
            way.id = field.value_uint64;
            break;
        case ID5WT3(2,2): // packed keys
            {
                int index = tags_begin;
                for(auto ptr = field.pointer; ptr < field.pointer + field.length; index++)
                {
                    if(!check_capacity(tags, index, "way tags"))
                    {
                        result = result_t::eoutofmem;
                        return false;
                    }
                    tags[index].key = string_table.get(read_varint_uint64(ptr));
                }
            }
            break;
        case ID5WT3(3,2): // packed values
            {
                int index = tags_begin;
                for(auto ptr = field.pointer; ptr < field.pointer + field.length; index++)
                {
                    if(!check_capacity(tags, index, "way tags"))
                    {
                        result = result_t::eoutofmem;
                        return false;
                    }
                    tags[index].value = string_table.get(read_varint_uint64(ptr));
                }
            }
            break;
        case ID5WT3(4,2): // way info
            if(decode_metadata)
                read_info<way_t>(way, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(8,2): // node refs
            {
                int64_t id = 0;
                size_t previous_capacity = node_refs.capacity();
                for(auto ptr = field.pointer; ptr < field.pointer + field.length;)
                {
                    id += read_varint_sint64(ptr);
                    node_refs.push_back(id);
                    if(node_refs.capacity() > previous_capacity)
                    {
                        if(verbose)
                            printf("way node ref capacity exceeded: %zu/%zu on thread %zu\n", node_refs.capacity(), previous_capacity, thread_index);
                        result = result_t::eoutofmem;
                        return false;
                    }
                }
            }
            break;
        }
       return true;
    }))
    {
        // error:
        if(result_t::eoutofmem == result)
        {
            // cleanup
            node_refs.erase(node_refs.begin() + node_ref_begin, node_refs.end());
            tags.erase(tags.begin() + tags_begin, tags.end());
        }
        else
            result = result_t::error;
    }
    else
    {
        // success:
        // node refs
        if(node_ref_begin != node_refs.size())
            way.node_refs = {node_refs.data() + node_ref_begin, node_refs.size() - node_ref_begin}; 
        // tags
        if(tags_begin != tags.size())
            way.tags = {tags.data() + tags_begin, tags.size() - tags_begin}; 
        // add to list
        way_list.emplace_back(way);
    }

    return result;
}

result_t read_relation(uint8_t* ptr, uint8_t* end, std::vector<relation_t>& relation_list, std::vector<tag_t>& tags, std::vector<relation_member_t>& members) noexcept
{
    relation_t relation;
    auto tags_begin = tags.size();
    auto members_begin = members.size();
    result_t result = result_t::ok;

    if(!iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,0): // relation id
            relation.id = field.value_uint64;
            break;
        case ID5WT3(2,2): // packed keys
            {
                int index = tags_begin;
                for(auto ptr = field.pointer; ptr < field.pointer + field.length; index++)
                {
                    if(!check_capacity(tags, index, "relation tags"))
                    {
                        result = result_t::eoutofmem;
                        return false;
                    }
                    tags[index].key = string_table.get(read_varint_uint64(ptr));
                }
            }
            break;
        case ID5WT3(3,2): // packed values
            {
                int index = tags_begin;
                for(auto ptr = field.pointer; ptr < field.pointer + field.length; index++)
                {
                    if(!check_capacity(tags, index, "relation tags"))
                    {
                        result = result_t::eoutofmem;
                        return false;
                    }
                    tags[index].value = string_table.get(read_varint_uint64(ptr));
                }
            }
            break;
        case ID5WT3(4,2): // relation info
            if(decode_metadata)
                read_info<relation_t>(relation, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(8,2): // member roles
            {
                int index = members_begin;
                for(auto ptr = field.pointer; ptr < field.pointer + field.length; index++)
                {
                    if(!check_capacity(members, index, "relation members"))
                    {
                        result = result_t::eoutofmem;
                        return false;
                    }
                    members[index].role = string_table.get(read_varint_uint64(ptr));
                }
            }
            break;
        case ID5WT3(9,2): // member ids
            {
                int index = members_begin;
                int64_t id = 0;
                for(auto ptr = field.pointer; ptr < field.pointer + field.length; index++)
                {
                    if(!check_capacity(members, index, "relation members"))
                    {
                        result = result_t::eoutofmem;
                        return false;
                    }
                    id += read_varint_sint64(ptr);
                    members[index].id = id;
                }
            }
            break;
        case ID5WT3(10,2): // member types
            {
                int index = members_begin;
                for(auto ptr = field.pointer; ptr < field.pointer + field.length; index++)
                {
                    if(!check_capacity(members, index, "relation members"))
                    {
                        result = result_t::eoutofmem;
                        return false;
                    }
                    members[index].type = read_varint_uint64(ptr);
                }
            }
            break;
        }
       return true;
    })) 
    {
        // error: 
        if(result_t::eoutofmem == result)
        {
            // cleanup
            tags.erase(tags.begin() + tags_begin, tags.end());
            members.erase(members.begin() + members_begin, members.end());
        }
        else
            result = result_t::error;
    }
    else
    {
        // success:
        // tags
        if(tags_begin != tags.size())
            relation.tags = {tags.data() + tags_begin, tags.size() - tags_begin}; 
        // members
        if(members_begin != members.size())
            relation.members = {members.data() + members_begin, members.size() - members_begin}; 
        // add to list
        relation_list.emplace_back(relation);
    }

    return result;
}

bool read_primitive_group(uint8_t* ptr, uint8_t* end) noexcept
{
    thread_local std::vector<way_t> way_list(8000);
    way_list.clear();
    thread_local std::vector<tag_t> way_tags(256000);
    way_tags.clear();
    thread_local std::vector<int64_t> way_node_refs(1024000);
    way_node_refs.clear();

    thread_local std::vector<relation_t> relation_list(1024);
    relation_list.clear();
    thread_local std::vector<tag_t> relation_tags(32000);
    relation_tags.clear();
    thread_local std::vector<relation_member_t> relation_members(128000);
    relation_members.clear();

    // read elements
    size_t nodes_read{0}, ways_read{0}, relations_read{0};
    bool restart_ways = true;
    bool restart_relations = true;
    bool result = true;
    while(restart_ways || restart_relations)
    {
        restart_ways = restart_relations = false;
        size_t node_index{0}, way_index{0}, relation_index{0};
        result = iterate_fields(ptr, end, [&](field_t& field)->bool{
            switch(field.id5wt3)
            {
            case ID5WT3(1,2): // node
                break;
            case ID5WT3(2,2): // dense nodes
                if(node_index++ >= nodes_read && node_handler)
                {
                    if(!read_dense_nodes(field.pointer, field.pointer + field.length))
                        return false;
                    nodes_read++;
                }
                break;
            case ID5WT3(3,2): // way
                if(way_index++ >= ways_read && way_handler)
                {
                    switch(read_way(field.pointer, field.pointer + field.length, way_list, way_tags, way_node_refs))
                    {
                        case result_t::eoutofmem:
                            restart_ways = true;
                        case result_t::error:
                            return false;
                    }
                    ways_read++;
                }
                break;
            case ID5WT3(4,2): // relation
                if(relation_index++ >= relations_read && relation_handler)
                {
                    switch(read_relation(field.pointer, field.pointer + field.length, relation_list, relation_tags, relation_members))
                    {
                        case result_t::eoutofmem:
                            restart_ways = true;
                        case result_t::error:
                            return false;
                    }
                    relations_read++;
                }
                break;
            }
            return true;
        });
        if(verbose && (restart_ways || restart_relations))
            printf("restarting read_primitive_group on thread %zu\n", thread_index);
    }
    if(result)
    {
        // report ways
        if(way_handler)
            if(!way_handler(span_t{way_list.data(), way_list.size()}))
                return false;
        
        // report relations
        if(relation_handler)
            if(!relation_handler(span_t{relation_list.data(), relation_list.size()}))
                return false;
    }
    return result;
}

bool read_primitve_block(uint8_t* ptr, uint8_t* end) noexcept
{
    // PrimitiveBlock
    string_table.clear();

    return iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,2): // string table
            string_table.init(field.length);
            if(!read_string_table(field.pointer, field.pointer + field.length))
                return false;
            break;
        case ID5WT3(2,2): // primitive group
            if(!read_primitive_group(field.pointer, field.pointer + field.length))
                return false;
            break;
        case ID5WT3(17,0): // granularity in nanodegrees
            granularity = (int64_t)field.value_uint64;
            if(verbose)
                std::cout << "granularity: " << granularity << " nanodegrees\n";
            break;
        case ID5WT3(18,0): // date granularity in milliseconds
            date_granularity = (int64_t)field.value_uint64;
            if(verbose)
                std::cout << "date granularity: " << date_granularity << " milliseconds\n";
            break;
        case ID5WT3(19,0): // latitude offset in nanodegrees
            lat_offset = (int64_t)field.value_uint64;
            if(verbose)
                std::cout << "latitude offset: " << lat_offset << " nanodegrees\n";
            break;
        case ID5WT3(20,0): // longitude offset in nanodegrees
            lon_offset = (int64_t)field.value_uint64;
            if(verbose)
                std::cout << "longitude offset: " << lon_offset << " nanodegrees\n";
            break;
        }
        return true;
    });
}

bool read_header_block(uint8_t* ptr, uint8_t* end) noexcept
{
    // HeaderBlock
    int64_t left{0}, right{0}, top{0}, bottom{0};
    std::vector<std::string> required_features;
    std::vector<std::string> optional_features;
    std::string writing_program, source;
    int64_t osmosis_replication_timestamp{0}, osmosis_sequence_number{0};
    std::string osmosis_replication_base_url;

    bool result = iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,2): // HeaderBBox
            {
                if(!iterate_fields(field.pointer, field.pointer + field.length, [&left, &right, &top, &bottom](field_t& field)->bool{
                    switch(field.id5wt3)
                    {
                    case ID5WT3(1,0): // left
                        left = to_sint64(field.value_uint64);
                        break;
                    case ID5WT3(2,0): // right
                        right = to_sint64(field.value_uint64);
                        break;
                    case ID5WT3(3,0): // top
                        top = to_sint64(field.value_uint64);
                        break;
                    case ID5WT3(4,0): // bottom
                        bottom = to_sint64(field.value_uint64);
                        break;
                    }
                    return true;
                }))
                    return false;
                if(verbose)
                {
                    std::cout << "left: " << std::fixed << std::setprecision(9) << left / 1e9 << "\n";
                    std::cout << "right: " << std::fixed << std::setprecision(9) << right / 1e9 << "\n";
                    std::cout << "top: " << std::fixed << std::setprecision(9) << top / 1e9 << "\n";
                    std::cout << "bottom: " << std::fixed << std::setprecision(9) << bottom / 1e9 << "\n";
                }
            }
            break;
        case ID5WT3(4,2): // required features
            required_features.emplace_back(std::string((const char*)field.pointer, field.length));
            if(verbose)
                std::cout << "required feature: " << required_features.back() << "\n";
            break;
        case ID5WT3(5,2): // optional features
            optional_features.emplace_back(std::string((const char*)field.pointer, field.length));
            if(verbose)
                std::cout << "optional feature: " << optional_features.back() << "\n";
            break;
        case ID5WT3(16,2): // writing program
            writing_program = std::string((const char*)field.pointer, field.length);
            if(verbose)
                std::cout << "writing_program: " << writing_program << "\n";
            break;
        case ID5WT3(17,2): // source
            source = std::string((const char*)field.pointer, field.length);
            if(verbose)
                std::cout << "source: " << source << "\n";
            break;
        case ID5WT3(32,0): // osmosis_replication_timestamp
            osmosis_replication_timestamp = field.value_uint64;
            if(verbose)
                std::cout << "osmosis_replication_timestamp: " << osmosis_replication_timestamp << " \"" << std::put_time(std::gmtime(&osmosis_replication_timestamp), "%Y-%m-%d %X %Z") << "\"\n";
            break;
        case ID5WT3(33,0): // osmosis_replication_sequence_number
            osmosis_sequence_number = field.value_uint64;
            if(verbose)
                std::cout << "osmosis_sequence_number: " << osmosis_sequence_number << "\n";
            break;
        case ID5WT3(34,0): // osmosis_replication_base_url
            osmosis_replication_base_url = std::string((const char*)field.pointer, field.length);
            if(verbose)
                std::cout << "osmosis_replication_base_url: " << osmosis_replication_base_url << "\n";
            break;
        }
        return true;
    });

    return result;
}

struct work_item
{
    uint8_t* buffer1 = nullptr;
    size_t blob_size = 0;
    bool (*handler)(uint8_t*, uint8_t*) = nullptr;
    size_t block_index = 0;
};
static std::queue<work_item> work_queue;
static std::mutex mtx_work_queue;

bool handle_blob(work_item &wi) noexcept;
bool work(size_t index) noexcept
{
    while(1)
    {
        input_osm::thread_index = std::min(index, thread_count() - 1);
        work_item wi;
        {
            std::lock_guard<std::mutex> lck(mtx_work_queue);
            if(work_queue.empty())
                return true;
            wi = work_queue.front();
            work_queue.pop();
        }
        input_osm::block_index = wi.block_index;
        if(!handle_blob(wi))
            return false;
    }
    return true;
}

bool handle_blob(work_item &wi) noexcept
{
    // Blob
    thread_local std::vector<uint8_t> buffer2;
    uint8_t *zip_ptr = nullptr;
    uint64_t zip_sz = 0;
    uint8_t *raw_ptr = nullptr;
    uint64_t raw_size = 0;
    iterate_fields(wi.buffer1, wi.buffer1 + wi.blob_size, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,2): // raw
            raw_size = field.length;
            raw_ptr = field.pointer;
            break;
        case ID5WT3(2,0): // raw size
            raw_size = field.value_uint64;
            break;
        case ID5WT3(3,2): // zlib_data
            zip_sz = field.length;
            zip_ptr = field.pointer;
            break;
        }
        return true;
    });

    // unzip if necessary
    if(zip_ptr && zip_sz && raw_size)
    {
        assert(zip_ptr >= wi.buffer1 && zip_ptr < wi.buffer1 + wi.blob_size);
        assert(zip_ptr + zip_sz <= wi.buffer1 + wi.blob_size);
        if(buffer2.size() < raw_size)
            buffer2.resize(raw_size);
        raw_ptr = buffer2.data();
        if(!unzip_compressed_block(zip_ptr, zip_sz, raw_ptr, raw_size))
        {
            return false;
        }
    }

    // use blob data
    bool result = true;
    if(wi.handler)
        result = wi.handler(raw_ptr, raw_ptr + raw_size);
    return result;
};

bool input_blob_mem(uint8_t* &buffer, uint8_t* buffer_end, uint32_t header_size, const char* expected_type, bool (*handler)(uint8_t*, uint8_t*), size_t index) noexcept
{
    // read BlobHeader
    uint8_t* header_buffer = buffer;
    buffer += header_size;
    if(buffer > buffer_end)
        return false;

    // BlobHeader
    bool expected_header_found = false;
    uint64_t blob_size = 0;
    size_t expected_type_len = strlen(expected_type);
    iterate_fields(header_buffer, header_buffer + header_size, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,2): // type
            expected_header_found = (field.length == expected_type_len) && (memcmp(field.pointer, expected_type, expected_type_len) == 0);
            break;
        case ID5WT3(3,0): // datasize
            blob_size = field.value_uint64;
            break;
        }
        return true;
    });
    if(!expected_header_found || !blob_size)
        return false;

    // read Blob
    uint8_t* buffer1 = buffer;
    buffer += blob_size;
    if(buffer > buffer_end)
        return false;

    // handle blob in its own thread
    work_queue.push(work_item{buffer1, blob_size, handler, index});
    return true;
}

static size_t g_thread_count = 0;
void set_thread_count(size_t count)
{
    g_thread_count = std::min(count, static_cast<size_t>(std::thread::hardware_concurrency()));
}
void set_max_thread_count()
{
    g_thread_count = std::thread::hardware_concurrency();
}
size_t thread_count()
{
    return g_thread_count ? g_thread_count : 1;
}

bool input_mem(uint8_t* file_begin, size_t file_size) noexcept
{
    // iterate file blocks
    {
        uint8_t* file_end = file_begin + file_size;    
        uint8_t* buf = file_begin;
        size_t index = 0;
        std::locale old_locale;

        if(verbose)
        {
            old_locale = std::cout.imbue(std::locale(""));
            std::cout << "file size is " << file_size << " bytes\n";
        }

        // header blob
        if(verbose)
        {
            std::cout << "\rreading block " << index;
            std::flush(std::cout);
        }
        if(buf + 4 > file_end)
            return false;
        uint32_t header_size = read_net_uint32(buf);
        buf += 4;
        if(!input_blob_mem(buf, file_end, header_size, "OSMHeader", read_header_block, index++))
            return false;

        // data blobs
        while(buf < file_end)
        {
            if(verbose)
            {
                std::cout << "\rreading block " << index << " offset " << buf - file_begin;
                std::flush(std::cout);
            }
            // header size
            if(buf + 4 > file_end)
                break;
            header_size = read_net_uint32(buf);
            buf += 4;
            // OSMData blob
            if(!input_blob_mem(buf, file_end, header_size, "OSMData", read_primitve_block, index++))
                return false;
        }
        if(verbose)
        {
            std::cout << "\nblock work queue has " << work_queue.size() << " items\n";
            std::cout.imbue(old_locale);
        }
    }

    // handle blobs
    if(thread_count() > 1)
    {
        // spawn workers
        std::vector<std::thread> worker_threads(thread_count());
        for(size_t index{0}; index < thread_count(); index++)
        {
            worker_threads[index] = std::thread(work, index);
        }

        // wait for them to finish
        for(auto& th: worker_threads)
        {
            if(th.joinable())
                th.join();
        }
    }
    else
    {
        // 1 thread, so call the work function directly
        work(0);
    }

    return true;
}

bool input_pbf(const char* filename) noexcept
{
    struct stat mmapstat;
    if(stat(filename, &mmapstat) == -1)
    {
        perror("stat");
        return false;
    }
    int fd;
    if((fd = open(filename, O_RDONLY)) == -1)
    {
        perror("open");
        return false;
    }
    uint8_t* file_data = (uint8_t*)mmap((caddr_t)0, mmapstat.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if((caddr_t)file_data == (caddr_t)(-1))
    {
        perror("mmap");
    }
    close(fd);
    if((caddr_t)file_data == (caddr_t)(-1))
    {
        return false;
    }
    bool result = input_mem(file_data, mmapstat.st_size);
    if(munmap(file_data, mmapstat.st_size) == -1)
    {
        perror("munmap");
        return false;
    }
    return result;
}

} // namespace
