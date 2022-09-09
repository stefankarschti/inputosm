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

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

static constexpr uint8_t ID5WT3(uint8_t id, uint8_t wt)
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

struct dense_info_t
{
    std::vector<uint32_t> version;
    std::vector<int64_t> timestamp;
    std::vector<int64_t> changeset;
    void clear()
    {
        version.clear();
        timestamp.clear();
        changeset.clear();
    }
};

struct field_t
{
    uint8_t id5wt3; // https://developers.google.com/protocol-buffers/docs/encoding#structure
    uint8_t *pointer;
    uint64_t length;
    uint64_t value_uint64;
};

struct string_table_t
{
    std::vector<size_t> st_index;
    std::vector<uint8_t> st_buffer;

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

inline uint32_t read_net_uint32(uint8_t *buf)
{
    return ((uint32_t)(buf[0]) << 24u) | ((uint32_t)(buf[1]) << 16u) | ((uint32_t)(buf[2]) << 8u) | ((uint32_t)(buf[3]));
}

inline uint64_t read_varint_uint64(uint8_t *&ptr)
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

inline int64_t to_sint64(uint64_t v64)
{
    return (v64 & 1) ? -(int64_t)((v64 + 1) / 2) : (v64 + 1) / 2;
}

inline uint64_t read_varint_sint64(uint8_t *&ptr)
{
    return to_sint64(read_varint_uint64(ptr));
}

inline int64_t read_varint_int64(uint8_t *&ptr)
{
    return (int64_t)read_varint_uint64(ptr);
}

inline uint8_t* read_field(uint8_t *ptr, field_t &field)
{
    field.id5wt3 = *ptr++;
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

inline bool unzip_compressed_block(uint8_t *zip_ptr, size_t zip_sz, uint8_t *raw_ptr, size_t raw_sz)
{
    uLongf size = raw_sz;
    int ret = uncompress(raw_ptr, &size, zip_ptr, zip_sz);
    return ret == Z_OK && size == raw_sz;
}

inline void read_sint64_packed(std::vector<int64_t>& packed, uint8_t* ptr, uint8_t* end)
{
    while(ptr < end)
        packed.emplace_back(read_varint_sint64(ptr));
}

inline void read_sint32_packed(std::vector<int32_t>& packed, uint8_t* ptr, uint8_t* end)
{
    while(ptr < end)
        packed.emplace_back(read_varint_sint64(ptr));
}

inline void read_uint32_packed(std::vector<uint32_t>& packed, uint8_t* ptr, uint8_t* end)
{
    while(ptr < end)
        packed.emplace_back(read_varint_uint64(ptr));
}

template <typename Handler>
inline bool iterate_fields(uint8_t* ptr, uint8_t* end, Handler&& handler)
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

bool read_string_table(string_table_t &string_table, uint8_t* ptr, uint8_t* end)
{
    return iterate_fields(ptr, end, [&](field_t& field)->bool{
        if(field.id5wt3 == ID5WT3(1, 2)) // string
            string_table.add(field.pointer, field.length);
        return true;
    }); 
}

bool read_dense_infos(dense_info_t &node_infos, uint8_t* ptr, uint8_t* end)
{
    return iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,2): // versions. not delta encoded
            read_uint32_packed(node_infos.version, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(2,2): // timestamps. delta encoded
            read_sint64_packed(node_infos.timestamp, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(3,2): // changesets. delta encoded
            read_sint64_packed(node_infos.changeset, field.pointer, field.pointer + field.length);
            break;
        }
        return true;
    });
}

bool read_dense_nodes(string_table_t &string_table, uint8_t* ptr, uint8_t* end)
{
    thread_local std::vector<int64_t> node_id;
    node_id.clear();
    thread_local std::vector<int64_t> latitude;
    latitude.clear();
    thread_local std::vector<int64_t> longitude;
    longitude.clear();
    thread_local std::vector<uint32_t> itags;
    itags.clear();
    thread_local dense_info_t node_infos;
    node_infos.clear();
    if(!iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,2): // node ids
            read_sint64_packed(node_id, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(5,2): // dense infos
            if(decode_metadata)
                read_dense_infos(node_infos, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(8,2): // latitudes
            read_sint64_packed(latitude, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(9,2): // longitudes
            read_sint64_packed(longitude, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(10,2): // packed indexes to keys & values
            read_uint32_packed(itags, field.pointer, field.pointer + field.length);
            break;
        }
        return true;
    }))
        return false;

    // decode nodes
    if(node_id.size() == latitude.size()
        && node_id.size() == longitude.size()
        && (!decode_metadata ||
        (node_id.size() == node_infos.version.size()
        && node_id.size() == node_infos.timestamp.size()
        && node_id.size() == node_infos.changeset.size())))
    {
        thread_local std::vector<node_t> node_list;
        node_list.clear();        
        node_t node;
        size_t itag = 0;
        thread_local std::vector<tag_t> tags;
        tags.clear();
        size_t node_id_size = node_id.size();
        auto p_node_id = node_id.data();
        auto p_node_id_end = p_node_id + node_id_size;
        auto p_latitude = latitude.data();
        auto p_longitude = longitude.data();
        for(size_t i = 0; p_node_id < p_node_id_end; i++)
        {
            // id
            node.id += *p_node_id++;
            // position
            node.raw_latitude += *p_latitude++;
            node.raw_longitude += *p_longitude++;
            // if(decode_node_coord)
            // {
            //     node.latitude = node.raw_latitude / 10000000.0;
            //     node.longitude = node.raw_longitude / 10000000.0;
            // }
            // tags
            const char* pkey = nullptr;
            const char* pval = nullptr;
            node.tags_begin_index = tags.size();
            for(;itag < itags.size(); itag++)
            {
                uint32_t istring = itags[itag];
                if(!istring)
                {
                    itag++;
                    break;
                }
                if(!pkey)
                {
                    pkey = string_table.get(istring);
                }
                else
                {
                    pval = string_table.get(istring);
                    tags.emplace_back(tag_t{pkey, pval});
                    pkey = pval = nullptr;
                }
            }
            node.tags_end_index = tags.size();
            // metadata
            // if(decode_metadata)
            // {
            //     node.version = node_infos.version[i];
            //     node.timestamp += node_infos.timestamp[i];
            //     node.changeset += node_infos.changeset[i];
            // }
            // add to node list
            node_list.push_back(node);
        }

        // fixup tag pointers
        for(node_t& n: node_list)
        {
            n.tags = {tags.data() + n.tags_begin_index, n.tags_end_index - n.tags_begin_index };
        }
        // report the node list
        if(node_handler)
            if(!node_handler(span_t{node_list.data(), node_list.size()}))
                return false;
    }
    else
        return false;
    return true;;
}

template <class T>
bool read_info(T &obj, uint8_t* ptr, uint8_t* end)
{
    return iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,0): // version
            // obj.version = field.value_uint64;
            break;
        case ID5WT3(2,0): // timestamp
            // obj.timestamp = field.value_uint64;
            break;
        case ID5WT3(3,0): // changeset
            // obj.changeset = field.value_uint64;
            break;
        }
        return true;
    });
}

bool read_way(string_table_t &string_table, uint8_t* ptr, uint8_t* end, std::vector<way_t>& way_list, std::vector<tag_t>& tags, std::vector<int64_t>& node_id)
{
    way_t way;
    thread_local std::vector<uint32_t> ikey;
    ikey.clear();
    thread_local std::vector<uint32_t> ivalue;
    ivalue.clear();
    way.node_refs_begin_index = node_id.size();

    if(!iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,0): // way id
            way.id = field.value_uint64;
            break;
        case ID5WT3(2,2): // packed keys
            read_uint32_packed(ikey, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(3,2): // packed values
            read_uint32_packed(ivalue, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(4,2): // way info
            if(decode_metadata)
                read_info<way_t>(way, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(8,2): // node ids
            read_sint64_packed(node_id, field.pointer, field.pointer + field.length);
            break;
        }
       return true;
    })) return false;

    if(ikey.size() != ivalue.size())
        return false;

    // decode way
    {
        // node refs
        int64_t current = 0;
        for(auto i = way.node_refs_begin_index; i < way.node_refs_end_index; ++i)
        {
            auto &n = node_id[i];
            current += n;
            n = current;
        } 
        way.node_refs_end_index = node_id.size();
        // tags
        way.tags_begin_index = tags.size();
        for(size_t i = 0; i < ikey.size(); ++i)
        {
            tags.emplace_back(tag_t{string_table.get(ikey[i]), string_table.get(ivalue[i])});
        }
        way.tags_end_index = tags.size();
        // add to list
        way_list.push_back(way);
    }

    return true;
}

bool read_relation(string_table_t &string_table, uint8_t* ptr, uint8_t* end, std::vector<relation_t>& relation_list, std::vector<tag_t>& tags, std::vector<relation_member_t>& members)
{
    relation_t relation;
    thread_local std::vector<uint32_t> ikey;
    ikey.clear();
    thread_local std::vector<uint32_t> ivalue;
    ivalue.clear();
    thread_local std::vector<uint32_t> member_role;
    member_role.clear();
    thread_local std::vector<int64_t> member_id;
    member_id.clear();
    thread_local std::vector<uint32_t> member_type;
    member_type.clear();

    if(!iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,0): // relation id
            relation.id = field.value_uint64;
            break;
        case ID5WT3(2,2): // packed keys
            read_uint32_packed(ikey, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(3,2): // packed values
            read_uint32_packed(ivalue, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(4,2): // relation info
            if(decode_metadata)
                read_info<relation_t>(relation, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(8,2): // member roles
            read_uint32_packed(member_role, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(9,2): // member ids
            read_sint64_packed(member_id, field.pointer, field.pointer + field.length);
            break;
        case ID5WT3(10,2): // member types
            read_uint32_packed(member_type, field.pointer, field.pointer + field.length);
            break;
        }
       return true;
    })) return false;

    if(ikey.size() != ivalue.size())
        return false;
    if((member_id.size() != member_role.size()) || (member_id.size() != member_role.size()))
        return false;

    // decode relation
    {
        // tags
        relation.tags_begin_index = tags.size();
        for(size_t i = 0; i < ikey.size(); ++i)
        {
            tags.emplace_back(tag_t{string_table.get(ikey[i]), string_table.get(ivalue[i])});
        }
        relation.tags_end_index = tags.size();
        // members
        relation.members_begin_index = members.size();
        int64_t current = 0;
        for(size_t i = 0; i < member_id.size(); ++i)
        {
            relation_member_t mem;
            current += member_id[i];
            mem.id = current;
            mem.role = string_table.get(member_role[i]);
            mem.type = member_type[i];
            members.emplace_back(mem);
        }
        relation.members_end_index = members.size();
        // add to list
        relation_list.push_back(relation);
    }

    return true;
}

bool read_primitive_group(string_table_t &string_table, uint8_t* ptr, uint8_t* end)
{
    thread_local std::vector<way_t> way_list;
    way_list.clear();
    thread_local std::vector<tag_t> way_tags;
    way_tags.clear();
    thread_local std::vector<int64_t> way_node_refs;
    way_node_refs.clear();
    thread_local std::vector<relation_t> relation_list;
    relation_list.clear();
    thread_local std::vector<tag_t> relation_tags;
    relation_tags.clear();
    thread_local std::vector<relation_member_t> relation_members;
    relation_members.clear();

    bool result = iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,2): // node
            break;
        case ID5WT3(2,2): // dense nodes
            if(node_handler)
            {
                if(!read_dense_nodes(string_table, field.pointer, field.pointer + field.length))
                    return false;
            }
            break;
        case ID5WT3(3,2): // way
            if(way_handler)
            {
                if(!read_way(string_table, field.pointer, field.pointer + field.length, way_list, way_tags, way_node_refs))
                    return false;
            }
            break;
        case ID5WT3(4,2): // relation
            if(relation_handler)
            {
                if(!read_relation(string_table, field.pointer, field.pointer + field.length, relation_list, relation_tags, relation_members))
                    return false;
            }
            break;
        }
        return true;
    });
    if(result)
    {
        // fixup way tag pointers
        for(way_t& w: way_list)
        {
            w.tags = {way_tags.data() + w.tags_begin_index, w.tags_end_index - w.tags_begin_index };
        }
        // fixup way node ref pointers
        for(way_t& w: way_list)
        {
            w.node_refs = {way_node_refs.data() + w.node_refs_begin_index, w.node_refs_end_index - w.node_refs_begin_index };
        }
        // report way list
        if(way_handler)
            if(!way_handler(span_t{way_list.data(), way_list.size()}))
                return false;
        // fixup relation tag pointers
        for(relation_t& r: relation_list)
        {
            r.tags = {relation_tags.data() + r.tags_begin_index, r.tags_end_index - r.tags_begin_index };
        }
        // fixup relation member pointers
        for(relation_t& r: relation_list)
        {
            r.members = {relation_members.data() + r.members_begin_index, r.members_end_index - r.members_begin_index };
        }
        // report relation list
        if(relation_handler)
            if(!relation_handler(span_t{relation_list.data(), relation_list.size()}))
                return false;
    }
    return result;
}

bool read_primitve_block(uint8_t* ptr, uint8_t* end)
{
    // PrimitiveBlock
    thread_local string_table_t string_table;
    string_table.init(end - ptr);
    return iterate_fields(ptr, end, [&](field_t& field)->bool{
        switch(field.id5wt3)
        {
        case ID5WT3(1,2): // string table
            if(!read_string_table(string_table, field.pointer, field.pointer + field.length))
                return false;
            break;
        case ID5WT3(2,2): // primitive group
            if(!read_primitive_group(string_table, field.pointer, field.pointer + field.length))
                return false;
            break;
        case ID5WT3(17,0): // granularity in nanodegrees
            break;
        case ID5WT3(18,0): // date granularity in milliseconds
            break;
        case ID5WT3(19,0): // latitude offset in nanodegrees
            break;
        case ID5WT3(20,0): // longitude offset in nanodegrees
            break;
        }
        return true;
    });
}

static std::queue<std::function<bool()>> work_queue;
static std::mutex mtx_work_queue;

bool input_blob_mem(uint8_t* &buffer, uint8_t* buffer_end, uint32_t header_size, const char* expected_type, std::function<bool(uint8_t*, uint8_t*)> handler)
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

    auto handle_blob = [buffer1, blob_size, handler]()->bool
    {
        // Blob
        thread_local std::vector<uint8_t> buffer2;
        uint8_t *zip_ptr = nullptr;
        uint64_t zip_sz = 0;
        uint8_t *raw_ptr = nullptr;
        uint64_t raw_size = 0;
        iterate_fields(buffer1, buffer1 + blob_size, [&](field_t& field)->bool{
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
            assert(zip_ptr >= buffer1 && zip_ptr < buffer1 + blob_size);
            assert(zip_ptr + zip_sz <= buffer1 + blob_size);
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
        if(handler)
            result = handler(raw_ptr, raw_ptr + raw_size);
        return result;
    };

    // handle blob in its own thread
    work_queue.push(handle_blob);
    return true;
}

size_t thread_count()
{
    return std::thread::hardware_concurrency();
}

bool work(size_t index)
{
    while(1)
    {
        thread_index = std::min(index, thread_count() - 1);
        std::function<bool()> handler;
        {
            std::lock_guard<std::mutex> lck(mtx_work_queue);
            if(work_queue.empty())
                return true;
            handler = work_queue.front();
            work_queue.pop();
        }
        if(!handler())
            return false;
    }
    return true;
}

bool input_mem(uint8_t* file_begin, size_t file_size)
{
    uint8_t* file_end = file_begin + file_size;    
    uint8_t* buf = file_begin;

    // header
    if(buf + 4 > file_end)
        return false;
    uint32_t header_size = read_net_uint32(buf);
    buf += 4;
    if(!input_blob_mem(buf, file_end, header_size, "OSMHeader", nullptr))
        return false;

    // Blobs
    while(buf < file_end)
    {
        // header size
        if(buf + 4 > file_end)
            break;
        header_size = read_net_uint32(buf);
        buf += 4;
        // OSMData blob
        if(!input_blob_mem(buf, file_end, header_size, "OSMData", read_primitve_block))
            return false;
    }

    // spawn workers
    std::vector<std::thread> worker_threads(thread_count());
    for(size_t index{0}; index < thread_count(); index++)
    {
        worker_threads[index] = std::thread(work, index);
    }

    // wait them to finish
    for(auto& th: worker_threads)
    {
        if(th.joinable())
            th.join();
    }

    return true;
}

bool input_pbf(const char* filename)
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
