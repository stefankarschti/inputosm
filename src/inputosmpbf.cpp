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

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include <libdeflate.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace input_osm
{
extern bool decode_metadata;
extern std::function<bool(std::span<const node_t>)> node_handler;
extern std::function<bool(std::span<const way_t>)> way_handler;
extern std::function<bool(std::span<const relation_t>)> relation_handler;

namespace
{
using bytes_t = std::span<const uint8_t>;
constexpr size_t max_header_size = 64 * 1024;
constexpr size_t max_raw_size = 32 * 1024 * 1024;
size_t configured_threads = 0;

[[noreturn, gnu::cold, gnu::noinline]] void invalid(const char* message)
{
    throw std::runtime_error(message);
}

void require(bool condition, const char* message)
{
    if (!condition) [[unlikely]]
        invalid(message);
}

struct field_t
{
    uint32_t number = 0;
    uint8_t wire = 0;
    uint64_t value = 0;
    bytes_t bytes;

    void expect(uint8_t expected) const { require(wire == expected, "Invalid field wire type"); }
    uint64_t integer() const
    {
        expect(0);
        return value;
    }
    bytes_t message() const
    {
        expect(2);
        return bytes;
    }
};

class reader_t
{
public:
    explicit reader_t(bytes_t bytes)
        : remaining_(bytes)
    {
    }
    bool empty() const { return remaining_.empty(); }
    bytes_t remaining() const { return remaining_; }
    bytes_t take(size_t count)
    {
        require(count <= remaining_.size(), "Truncated PBF field");
        auto result = remaining_.first(count);
        remaining_ = remaining_.subspan(count);
        return result;
    }
    [[gnu::always_inline]] inline uint64_t varint()
    {
        const auto* data = remaining_.data();
        const auto size = remaining_.size();
        require(size != 0, "Truncated PBF field");
        uint64_t result = data[0] & 0x7f;
        if (data[0] < 0x80)
        {
            remaining_ = remaining_.subspan(1);
            return result;
        }
        for (size_t index = 1; index < 10; ++index)
        {
            require(index < size, "Truncated PBF field");
            const auto byte = data[index];
            require(index != 9 || byte <= 1, "PBF varint overflow");
            result |= uint64_t(byte & 0x7f) << (7 * index);
            if (!(byte & 0x80))
            {
                remaining_ = remaining_.subspan(index + 1);
                return result;
            }
        }
        throw std::runtime_error("Invalid PBF varint");
    }
    [[gnu::always_inline]] inline field_t next(unsigned group_depth = 0)
    {
        const auto key = varint();
        require(key >> 3 != 0 && key >> 3 <= 0x1fffffff, "Invalid PBF field number");
        field_t field;
        field.number = static_cast<uint32_t>(key >> 3);
        field.wire = key & 7;
        switch (field.wire)
        {
            case 0:
                field.value = varint();
                break;
            case 1:
                field.bytes = take(8);
                break;
            case 2: {
                const auto size = varint();
                require(size <= remaining_.size(), "Truncated PBF message");
                field.bytes = take(static_cast<size_t>(size));
                break;
            }
            case 3:
                skip_group(field.number, group_depth);
                break;
            case 4:
                require(group_depth != 0, "Unexpected PBF group end");
                break;
            case 5:
                field.bytes = take(4);
                break;
            default:
                throw std::runtime_error("Unsupported PBF wire type");
        }
        return field;
    }

private:
    [[gnu::cold, gnu::noinline]] void skip_group(uint32_t number, unsigned depth)
    {
        require(depth < 64, "PBF group nesting limit exceeded");
        for (;;)
        {
            const auto nested = next(depth + 1);
            if (nested.wire != 4) continue;
            require(nested.number == number, "Invalid PBF group end");
            return;
        }
    }
    bytes_t remaining_;
};

int64_t signed_integer(uint64_t value)
{
    return std::bit_cast<int64_t>(value);
}
int64_t zigzag(uint64_t value)
{
    return signed_integer((value >> 1) ^ (uint64_t{0} - (value & 1)));
}
int32_t narrow(int64_t value)
{
    require(value >= std::numeric_limits<int32_t>::min() && value <= std::numeric_limits<int32_t>::max(),
            "PBF value exceeds the public field range");
    return static_cast<int32_t>(value);
}
int64_t add_delta(int64_t previous, int64_t delta)
{
    int64_t result;
    require(!__builtin_add_overflow(previous, delta, &result), "PBF delta overflow");
    return result;
}
std::string_view text(bytes_t bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
template <class Consume>
void repeated(const field_t& field, Consume consume)
{
    if (field.wire == 0)
    {
        consume(field.value);
        return;
    }
    reader_t reader(field.message());
    while (!reader.empty()) consume(reader.varint());
}

template <class T>
T& column_item(std::vector<T>& output, size_t index)
{
    // Each column advances by one element from the same start position.
    if (index == output.size()) output.emplace_back();
    return output[index];
}

struct range_t
{
    size_t start = 0;
    size_t size = 0;
};

struct node_tag_range_t
{
    size_t index;
    size_t start;
    size_t size;
};

struct storage_t
{
    std::vector<node_t> nodes;
    std::vector<way_t> ways;
    std::vector<relation_t> relations;
    std::vector<tag_t> tags;
    std::vector<int64_t> refs;
    std::vector<relation_member_t> members;
    std::vector<node_tag_range_t> node_tags;
    std::vector<range_t> way_tags, relation_tags, way_refs, relation_members;

    void clear()
    {
        nodes.clear();
        ways.clear();
        relations.clear();
        tags.clear();
        refs.clear();
        members.clear();
        node_tags.clear();
        way_tags.clear();
        relation_tags.clear();
        way_refs.clear();
        relation_members.clear();
    }
    void finish()
    {
        const std::span<const tag_t> tag_span(tags);
        for (const auto& range : node_tags) nodes[range.index].tags = tag_span.subspan(range.start, range.size);
        for (size_t i = 0; i < ways.size(); ++i)
        {
            ways[i].tags = tag_span.subspan(way_tags[i].start, way_tags[i].size);
            ways[i].node_refs = std::span<const int64_t>(refs).subspan(way_refs[i].start, way_refs[i].size);
        }
        for (size_t i = 0; i < relations.size(); ++i)
        {
            relations[i].tags = tag_span.subspan(relation_tags[i].start, relation_tags[i].size);
            relations[i].members = std::span<const relation_member_t>(members).subspan(relation_members[i].start,
                                                                                       relation_members[i].size);
        }
    }
};

struct blob_decoder_t
{
    std::unique_ptr<libdeflate_decompressor, decltype(&libdeflate_free_decompressor)> decompressor{
        nullptr, libdeflate_free_decompressor};
    std::unique_ptr<uint8_t[]> buffer;
    size_t capacity = 0;

    bytes_t decompress(bytes_t payload, size_t raw_size)
    {
        if (!decompressor)
        {
            decompressor.reset(libdeflate_alloc_decompressor());
            require(decompressor != nullptr, "Cannot allocate PBF decompressor");
        }
        const auto required = std::max<size_t>(1, raw_size);
        if (required > capacity)
        {
            // The decompressor overwrites this buffer before the reader uses it.
            buffer.reset(new uint8_t[required]);
            capacity = required;
        }
        size_t input_size = 0;
        // A null output count requires exactly raw_size decompressed bytes.
        const auto result = libdeflate_zlib_decompress_ex(
            decompressor.get(), payload.data(), payload.size(), buffer.get(), raw_size, &input_size, nullptr);
        require(result == LIBDEFLATE_SUCCESS && input_size == payload.size(), "Invalid Zlib Blob data");
        return {buffer.get(), raw_size};
    }
};

template <class Visit>
unsigned visit_group(bytes_t bytes, Visit&& visit)
{
    unsigned kind = 0;
    reader_t reader(bytes);
    while (!reader.empty())
    {
        const auto field = reader.next();
        if (field.number >= 1 && field.number <= 4)
        {
            require(kind == 0 || kind == field.number, "Mixed entity types in a primitive group");
            kind = field.number;
            field.expect(2);
            visit(field);
        }
        else if (field.number == 5)
            invalid("Unsupported ChangeSet entity");
    }
    return kind;
}

struct decoder_t
{
    bool metadata;
    std::vector<std::string_view> string_table;
    std::vector<bytes_t> groups, string_messages;
    bool strings_ready = false;
    // The columns contain IDs, coordinates, tags, and the six DenseInfo fields.
    std::array<std::vector<field_t>, 10> dense_fields;
    blob_decoder_t raw;
    storage_t storage;
    pbf_parameters_t block;

    explicit decoder_t(bool read_metadata)
        : metadata(read_metadata)
    {
    }

    std::string_view string(uint64_t index) const
    {
        require(index <= std::numeric_limits<uint32_t>::max() && index < string_table.size(),
                "Invalid PBF string index");
        return string_table[static_cast<size_t>(index)];
    }
    void read_tags(const field_t& field, size_t start, size_t& count, bool keys)
    {
        repeated(field, [&](uint64_t value) {
            const auto view = string(value);
            auto& tag = column_item(storage.tags, start + count++);
            if (keys)
                tag.key = view;
            else
                tag.value = view;
        });
    }
    template <class Entity>
    void read_info(bytes_t bytes, Entity& entity)
    {
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            if (!metadata) continue;
            switch (field.number)
            {
                case 1:
                    entity.version = narrow(signed_integer(field.integer()));
                    break;
                case 2:
                    entity.timestamp = narrow(signed_integer(field.integer()));
                    break;
                case 3:
                    entity.changeset = narrow(signed_integer(field.integer()));
                    break;
                case 5:
                    (void)string(field.integer());
                    break;
                default:
                    break;
            }
        }
    }
    void node(bytes_t bytes)
    {
        node_t result;
        const auto start = storage.tags.size();
        size_t keys = 0, values = 0;
        unsigned required = 0;
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            switch (field.number)
            {
                case 1:
                    result.id = zigzag(field.integer());
                    required |= 1;
                    break;
                case 2:
                    read_tags(field, start, keys, true);
                    break;
                case 3:
                    read_tags(field, start, values, false);
                    break;
                case 4:
                    read_info(field.message(), result);
                    break;
                case 8:
                    result.raw_latitude = zigzag(field.integer());
                    required |= 2;
                    break;
                case 9:
                    result.raw_longitude = zigzag(field.integer());
                    required |= 4;
                    break;
                default:
                    break;
            }
        }
        require(required == 7, "Missing required node field");
        require(keys == values, "PBF tag arrays have different sizes");
        if (keys != 0) storage.node_tags.push_back({storage.nodes.size(), start, keys});
        storage.nodes.push_back(result);
    }
    void way(bytes_t bytes)
    {
        way_t result;
        const auto tag_start = storage.tags.size(), ref_start = storage.refs.size();
        size_t keys = 0, values = 0;
        int64_t ref = 0;
        bool has_id = false;
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            switch (field.number)
            {
                case 1:
                    result.id = signed_integer(field.integer());
                    has_id = true;
                    break;
                case 2:
                    read_tags(field, tag_start, keys, true);
                    break;
                case 3:
                    read_tags(field, tag_start, values, false);
                    break;
                case 4:
                    read_info(field.message(), result);
                    break;
                case 8:
                    repeated(field, [&](uint64_t value) {
                        ref = add_delta(ref, zigzag(value));
                        storage.refs.push_back(ref);
                    });
                    break;
                default:
                    break;
            }
        }
        require(has_id, "Missing required way ID");
        require(keys == values, "PBF tag arrays have different sizes");
        storage.way_refs.push_back({ref_start, storage.refs.size() - ref_start});
        storage.way_tags.push_back({tag_start, keys});
        storage.ways.push_back(result);
    }
    void relation(bytes_t bytes)
    {
        relation_t result;
        const auto tag_start = storage.tags.size(), member_start = storage.members.size();
        size_t keys = 0, values = 0, roles = 0, ids = 0, types = 0;
        int64_t id = 0;
        bool has_id = false;
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            switch (field.number)
            {
                case 1:
                    result.id = signed_integer(field.integer());
                    has_id = true;
                    break;
                case 2:
                    read_tags(field, tag_start, keys, true);
                    break;
                case 3:
                    read_tags(field, tag_start, values, false);
                    break;
                case 4:
                    read_info(field.message(), result);
                    break;
                case 8:
                    repeated(field, [&](uint64_t value) {
                        const auto role = string(value);
                        column_item(storage.members, member_start + roles++).role = role;
                    });
                    break;
                case 9:
                    repeated(field, [&](uint64_t value) {
                        id = add_delta(id, zigzag(value));
                        column_item(storage.members, member_start + ids++).id = id;
                    });
                    break;
                case 10:
                    repeated(field, [&](uint64_t value) {
                        require(value <= 2, "Invalid PBF relation member type");
                        column_item(storage.members, member_start + types++).type = static_cast<uint8_t>(value);
                    });
                    break;
                default:
                    break;
            }
        }
        require(has_id, "Missing required relation ID");
        require(ids == roles && ids == types, "Invalid PBF member array sizes");
        require(keys == values, "PBF tag arrays have different sizes");
        storage.relation_members.push_back({member_start, ids});
        storage.relation_tags.push_back({tag_start, keys});
        storage.relations.push_back(result);
    }
    void collect_dense(bytes_t bytes, bool skip_info = false)
    {
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            switch (field.number)
            {
                case 1:
                    dense_fields[0].push_back(field);
                    break;
                case 8:
                case 9:
                case 10:
                    dense_fields[field.number - 7].push_back(field);
                    break;
                case 5: {
                    if (skip_info && !metadata) break;
                    reader_t info_reader(field.message());
                    while (!info_reader.empty())
                    {
                        const auto entry = info_reader.next();
                        if (metadata && entry.number >= 1 && entry.number <= 6)
                            dense_fields[entry.number + 3].push_back(entry);
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
    template <class Consume>
    void dense_column(size_t column, size_t start, Consume consume)
    {
        size_t index = start;
        for (const auto& field : dense_fields[column])
            repeated(field, [&](uint64_t value) {
                require(index < storage.nodes.size(), "Invalid dense array size");
                consume(storage.nodes[index++], value);
            });
        require(index == storage.nodes.size() || (column >= 4 && index == start), "Invalid dense array size");
    }
    void dense()
    {
        const size_t start = storage.nodes.size();
        const bool single_packed = dense_fields[0].size() == 1 && dense_fields[1].size() == 1 &&
                                   dense_fields[2].size() == 1 && dense_fields[0][0].wire == 2 &&
                                   dense_fields[1][0].wire == 2 && dense_fields[2][0].wire == 2;
        if (single_packed)
        {
            reader_t ids(dense_fields[0][0].message()), latitudes(dense_fields[1][0].message()),
                longitudes(dense_fields[2][0].message());
            int64_t id = 0, latitude = 0, longitude = 0;
            while (!ids.empty())
            {
                require(!latitudes.empty() && !longitudes.empty(), "Invalid dense array size");
                node_t node;
                node.id = id = add_delta(id, zigzag(ids.varint()));
                node.raw_latitude = latitude = add_delta(latitude, zigzag(latitudes.varint()));
                node.raw_longitude = longitude = add_delta(longitude, zigzag(longitudes.varint()));
                storage.nodes.push_back(node);
            }
            require(latitudes.empty() && longitudes.empty(), "Invalid dense array size");
        }
        else
        {
            int64_t id = 0;
            for (const auto& field : dense_fields[0])
                repeated(field, [&](uint64_t value) {
                    id = add_delta(id, zigzag(value));
                    storage.nodes.emplace_back().id = id;
                });
            int64_t latitude = 0, longitude = 0;
            dense_column(1, start, [&](node_t& node, uint64_t value) {
                node.raw_latitude = latitude = add_delta(latitude, zigzag(value));
            });
            dense_column(2, start, [&](node_t& node, uint64_t value) {
                node.raw_longitude = longitude = add_delta(longitude, zigzag(value));
            });
        }
        if (metadata)
        {
            int64_t timestamp = 0, changeset = 0, uid = 0, user = 0;
            dense_column(4, start, [](node_t& node, uint64_t value) { node.version = narrow(signed_integer(value)); });
            dense_column(5, start, [&](node_t& node, uint64_t value) {
                node.timestamp = narrow(timestamp = add_delta(timestamp, zigzag(value)));
            });
            dense_column(6, start, [&](node_t& node, uint64_t value) {
                node.changeset = narrow(changeset = add_delta(changeset, zigzag(value)));
            });
            dense_column(7, start, [&](node_t&, uint64_t value) { (void)narrow(uid = add_delta(uid, zigzag(value))); });
            dense_column(8, start, [&](node_t&, uint64_t value) {
                (void)string(static_cast<uint64_t>(user = add_delta(user, zigzag(value))));
            });
            dense_column(9, start, [](node_t&, uint64_t) {});
        }
        size_t node_index = start, tag_start = storage.tags.size();
        std::string_view key;
        bool has_key = false, has_tags = false;
        for (const auto& field : dense_fields[3])
            repeated(field, [&](uint64_t value) {
                has_tags = true;
                require(node_index < storage.nodes.size(), "Extra dense node tags");
                if (has_key)
                {
                    storage.tags.push_back({key, string(value)});
                    has_key = false;
                }
                else if (value == 0)
                {
                    if (storage.tags.size() != tag_start)
                        storage.node_tags.push_back({node_index, tag_start, storage.tags.size() - tag_start});
                    ++node_index;
                    tag_start = storage.tags.size();
                }
                else
                {
                    key = string(value);
                    has_key = true;
                }
            });
        require(!has_key, "Missing dense tag value");
        require(!has_tags || node_index == storage.nodes.size(), "Missing dense tag delimiter");
    }
    bool group(bytes_t bytes)
    {
        for (auto& fields : dense_fields) fields.clear();
        const auto kind = visit_group(bytes, [&](const field_t& field) {
            switch (field.number)
            {
                case 1:
                    node(field.message());
                    break;
                case 2:
                    collect_dense(field.message());
                    break;
                case 3:
                    way(field.message());
                    break;
                case 4:
                    relation(field.message());
                    break;
            }
        });
        if (kind == 2) dense();
        return kind == 1 || kind == 2;
    }
    void prepare(bytes_t bytes)
    {
        storage.clear();
        string_table.clear();
        groups.clear();
        string_messages.clear();
        strings_ready = false;
        block = {};
        bool has_table = false;
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            switch (field.number)
            {
                case 1:
                    has_table = true;
                    string_messages.push_back(field.message());
                    break;
                case 2:
                    groups.push_back(field.message());
                    break;
                case 17:
                    block.granularity = narrow(signed_integer(field.integer()));
                    break;
                case 18:
                    block.date_granularity = narrow(signed_integer(field.integer()));
                    break;
                case 19:
                    block.lat_offset = signed_integer(field.integer());
                    break;
                case 20:
                    block.lon_offset = signed_integer(field.integer());
                    break;
                default:
                    break;
            }
        }
        require(has_table, "Missing PBF string table");
        require(block.granularity > 0 && block.date_granularity > 0, "Invalid PBF granularity");
    }
    void ensure_strings()
    {
        if (strings_ready) return;
        string_table.clear();
        for (auto bytes : string_messages)
        {
            reader_t strings(bytes);
            while (!strings.empty())
            {
                auto field = strings.next();
                if (field.number == 1) string_table.push_back(text(field.message()));
            }
        }
        require(!string_table.empty() && string_table[0].empty(), "Invalid PBF string table");
        strings_ready = true;
    }
};

#include "pbfcolumns.h"

struct descriptor_t
{
    bytes_t blob;
    std::string_view type;
    size_t index = 0;
    uint64_t offset = 0;
};

descriptor_t read_descriptor(reader_t& reader, size_t index, uint64_t offset)
{
    descriptor_t result;
    result.index = index;
    result.offset = offset;
    const auto prefix = reader.take(4);
    const auto size = (uint32_t(prefix[0]) << 24) | (uint32_t(prefix[1]) << 16) | (uint32_t(prefix[2]) << 8) |
                      prefix[3];
    require(size > 0 && size < max_header_size, "Invalid BlobHeader size");
    const auto header_bytes = reader.take(size);
#ifndef INPUTOSM_BENCH_GENERIC_HEADERS
    // Most writers put the data type before the Blob size without other fields.
    if (size >= 11 && header_bytes[0] == 0x0a &&
        std::memcmp(header_bytes.data() + 1,
                    "\x07"
                    "OSMData",
                    8) == 0 &&
        header_bytes[9] == 0x18)
    {
        reader_t length(header_bytes.subspan(10));
        const auto value = length.varint();
        if (length.empty())
        {
            const auto blob_size = narrow(signed_integer(value));
            require(blob_size >= 0, "Invalid BlobHeader");
            result.type = text(header_bytes.subspan(2, 7));
            result.blob = reader.take(static_cast<size_t>(blob_size));
            return result;
        }
    }
#endif
    reader_t header(header_bytes);
    int32_t blob_size = 0;
    bool has_type = false, has_size = false;
    while (!header.empty())
    {
        const auto field = header.next();
        if (field.number == 1)
        {
            result.type = text(field.message());
            has_type = true;
        }
        if (field.number == 3)
        {
            blob_size = narrow(signed_integer(field.integer()));
            has_size = true;
        }
    }
    require(has_type && !result.type.empty() && has_size && blob_size >= 0, "Invalid BlobHeader");
    result.blob = reader.take(static_cast<size_t>(blob_size));
    return result;
}

bytes_t blob_data(bytes_t blob, blob_decoder_t& decoder)
{
    reader_t reader(blob);
    bytes_t payload;
    uint32_t encoding = 0;
    int32_t raw_size = -1;
    bool has_raw_size = false;
    while (!reader.empty())
    {
        const auto field = reader.next();
        if (field.number == 2)
        {
            raw_size = narrow(signed_integer(field.integer()));
            has_raw_size = true;
            require(raw_size >= 0, "Negative uncompressed Blob size");
        }
        if (field.number == 1 || (field.number >= 3 && field.number <= 7))
        {
            require(encoding == 0, "Multiple Blob encodings");
            encoding = field.number;
            payload = field.message();
        }
    }
    require(encoding == 1 || encoding == 3, "Unsupported or missing Blob encoding");
    if (encoding == 1)
    {
        require(payload.size() < max_raw_size, "PBF raw size limit exceeded");
        require(!has_raw_size || static_cast<size_t>(raw_size) == payload.size(), "Incorrect raw Blob size");
        return payload;
    }
    require(has_raw_size && static_cast<size_t>(raw_size) < max_raw_size, "Invalid uncompressed Blob size");
    return decoder.decompress(payload, static_cast<size_t>(raw_size));
}

void validate_header(bytes_t bytes)
{
    reader_t reader(bytes);
    unsigned bbox_present = 0;
    bool has_bbox = false;
    while (!reader.empty())
    {
        const auto field = reader.next();
        if (field.number == 4)
        {
            const auto feature = text(field.message());
            if (feature != "OsmSchema-V0.6" && feature != "DenseNodes")
                throw std::runtime_error("Unsupported required PBF feature: " + std::string(feature));
        }
        if (field.number == 1)
        {
            has_bbox = true;
            reader_t bbox(field.message());
            while (!bbox.empty())
            {
                const auto coordinate = bbox.next();
                if (coordinate.number >= 1 && coordinate.number <= 4)
                {
                    (void)coordinate.integer();
                    bbox_present |= 1u << (coordinate.number - 1);
                }
            }
        }
    }
    require(!has_bbox || bbox_present == 15, "Missing PBF bounding box coordinate");
}

class mapping_t
{
public:
    mapping_t(int fd, const struct stat& status)
        : status_(status),
          size_(static_cast<size_t>(status.st_size))
    {
        void* address = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        require(address != MAP_FAILED, "Cannot map PBF file");
        data_ = static_cast<const uint8_t*>(address);
    }
    ~mapping_t() { release(); }
    mapping_t(const mapping_t&) = delete;
    mapping_t& operator=(const mapping_t&) = delete;
    bytes_t bytes() const { return {data_, size_}; }
    bool same_file(const struct stat& other) const
    {
#if defined(__APPLE__)
        const auto& modified = status_.st_mtimespec;
        const auto& other_modified = other.st_mtimespec;
        const auto& changed = status_.st_ctimespec;
        const auto& other_changed = other.st_ctimespec;
#else
        const auto& modified = status_.st_mtim;
        const auto& other_modified = other.st_mtim;
        const auto& changed = status_.st_ctim;
        const auto& other_changed = other.st_ctim;
#endif
        return status_.st_dev == other.st_dev && status_.st_ino == other.st_ino && status_.st_size == other.st_size &&
               modified.tv_sec == other_modified.tv_sec && modified.tv_nsec == other_modified.tv_nsec &&
               changed.tv_sec == other_changed.tv_sec && changed.tv_nsec == other_changed.tv_nsec;
    }
    bool release() noexcept
    {
        if (!data_) return true;
        const int result = munmap(const_cast<uint8_t*>(data_), size_);
        data_ = nullptr;
        if (result != 0) IOSM_ERROR("Cannot release PBF mapping");
        return result == 0;
    }

private:
    struct stat status_{};
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

std::shared_ptr<mapping_t> open_mapping(const char* filename)
{
    require(filename != nullptr, "Null PBF filename");
    struct file_handle_t
    {
        int fd;
        ~file_handle_t()
        {
            if (fd >= 0) ::close(fd);
        }
        void close()
        {
            const auto descriptor = std::exchange(fd, -1);
            require(::close(descriptor) == 0, "Cannot close PBF file descriptor");
        }
    } file{::open(filename, O_RDONLY | O_CLOEXEC)};
    require(file.fd >= 0, "Cannot open PBF file");
    struct stat status{};
    require(fstat(file.fd, &status) == 0 && S_ISREG(status.st_mode) && status.st_size > 0 &&
                static_cast<uint64_t>(status.st_size) <= static_cast<uint64_t>(std::numeric_limits<ptrdiff_t>::max()),
            "Invalid PBF file size or type");
#ifndef INPUTOSM_BENCH_INDEPENDENT_MAPS
    // Readers share immutable file pages. Each reader keeps its own decoder.
    static std::mutex mutex;
    static std::vector<std::weak_ptr<mapping_t>> mappings;
    const std::lock_guard lock(mutex);
    for (auto entry = mappings.begin(); entry != mappings.end();)
    {
        if (auto previous = entry->lock())
        {
            if (previous->same_file(status))
            {
                file.close();
                return previous;
            }
            ++entry;
        }
        else
            entry = mappings.erase(entry);
    }
#endif
    auto mapping = std::make_shared<mapping_t>(file.fd, status);
    file.close();
#ifndef INPUTOSM_BENCH_INDEPENDENT_MAPS
    mappings.push_back(mapping);
#endif
    return mapping;
}

struct callback_scope_t
{
    size_t previous_thread = thread_index;
    size_t previous_block = block_index;

    callback_scope_t(size_t worker, size_t index)
    {
        thread_index = worker;
        block_index = index;
    }
    ~callback_scope_t()
    {
        thread_index = previous_thread;
        block_index = previous_block;
    }
};

struct scratch_t
{
    bool active = false;
    decoder_t decoder{false};
    columns_t columns;
};
thread_local std::vector<std::unique_ptr<scratch_t>> scratch_pool;

struct block_state_t
{
    descriptor_t descriptor;
    bool locations_allowed;
    const std::thread::id owner = std::this_thread::get_id();
    std::atomic<bool>* stopped;
    bool failed = false, busy = false;
    scratch_t* scratch = nullptr;
    unsigned count_mask = 0;
    pbf_counts_t cached_counts{};
    std::optional<size_t> string_count{};

    ~block_state_t()
    {
        if (scratch)
        {
            scratch->decoder.groups.clear();
            scratch->decoder.string_messages.clear();
            scratch->decoder.string_table.clear();
            for (auto& fields : scratch->decoder.dense_fields) fields.clear();
            scratch->decoder.storage.clear();
            scratch->columns.clear();
            scratch->active = false;
        }
    }
    decoder_t& prepare()
    {
        if (scratch) return scratch->decoder;
        for (auto& slot : scratch_pool)
            if (!slot->active)
            {
                scratch = slot.get();
                break;
            }
        if (!scratch)
        {
            scratch_pool.push_back(std::make_unique<scratch_t>());
            scratch = scratch_pool.back().get();
        }
        scratch->active = true;
        auto& decoder = scratch->decoder;
        decoder.prepare(blob_data(descriptor.blob, decoder.raw));
        return decoder;
    }
    bool active() const { return !failed && (!stopped || !stopped->load(std::memory_order_relaxed)); }
    template <class Work>
    bool run(Work&& work) noexcept
    {
        try
        {
            require(owner == std::this_thread::get_id(), "PBF block used from another thread");
            require(!busy, "Recursive PBF block operation");
            if (!active()) return false;
            busy = true;
            struct reset_t
            {
                bool& value;
                ~reset_t() { value = false; }
            } reset{busy};
            if (work() && active()) return true;
        }
        catch (const std::exception& error)
        {
            IOSM_ERROR("PBF block {} at offset {}: {}", descriptor.index, descriptor.offset, error.what());
        }
        catch (...)
        {
            IOSM_ERROR("PBF block {}: callback exception", descriptor.index);
        }
        failed = true;
        if (stopped) stopped->store(true, std::memory_order_relaxed);
        return false;
    }
    pbf_counts_t count(unsigned mask)
    {
        mask &= ~count_mask;
        if (!mask) return cached_counts;
        auto& decoder = prepare();
        auto result = cached_counts;
        for (auto group : decoder.groups)
        {
            require(active(), "PBF operation stopped");
            visit_group(group, [&](const field_t& field) {
                if (field.number == 1 && (mask & 1)) ++result.nodes;
                if (field.number == 3 && (mask & 2)) ++result.ways;
                if (field.number == 4 && (mask & 4)) ++result.relations;
                if (field.number == 2 && (mask & 1))
                {
                    reader_t dense(field.message());
                    while (!dense.empty())
                    {
                        const auto column = dense.next();
                        if (column.number == 1) result.nodes += value_count(column);
                    }
                }
            });
        }
        cached_counts = result;
        count_mask |= mask;
        return result;
    }
    template <class Visit>
    bool strings(Visit&& visit)
    {
        size_t count = 0;
        for (auto bytes : prepare().string_messages)
        {
            reader_t reader(bytes);
            while (!reader.empty())
            {
                const auto field = reader.next();
                if (field.number != 1) continue;
                const auto value = text(field.message());
                require(count != 0 || value.empty(), "PBF string zero must be empty");
                ++count;
                if (!visit(value)) return false;
            }
        }
        require(count != 0, "Empty PBF string table");
        string_count = count;
        return true;
    }
    bool decode(const pbf_entity_options_t& options, const pbf_group_handler_t& handler)
    {
        auto& decoder = prepare();
        auto& columns = scratch->columns;
        pbf_counts_t totals;
        for (size_t index = 0; index < decoder.groups.size(); ++index)
        {
            if (!active()) return false;
            columns.clear();
            for (auto& fields : decoder.dense_fields) fields.clear();
            decoder.metadata = options.nodes && options.nodes->metadata;
            const auto kind = visit_group(decoder.groups[index], [&](const field_t& field) {
                if (field.number == 2)
                {
                    if (options.nodes) decoder.collect_dense(field.message(), true);
                }
                else if ((field.number == 1 && options.nodes) || (field.number == 3 && options.ways) ||
                         (field.number == 4 && options.relations))
                    columns.ordinary(field.message(), field.number, options, locations_allowed);
            });
            if (kind == 2 && options.nodes) columns.dense(decoder.dense_fields, *options.nodes);
            columns.empty_lists(kind, options);
            if (kind <= 2) totals.nodes += columns.count;
            if (kind == 3) totals.ways += columns.count;
            if (kind == 4) totals.relations += columns.count;
            if (!handler(columns.batch(descriptor.index, index, kind, decoder.block, options))) return false;
        }
        if (options.nodes)
        {
            cached_counts.nodes = totals.nodes;
            count_mask |= 1;
        }
        if (options.ways)
        {
            cached_counts.ways = totals.ways;
            count_mask |= 2;
        }
        if (options.relations)
        {
            cached_counts.relations = totals.relations;
            count_mask |= 4;
        }
        return true;
    }
};
} // namespace

struct pbf_access_t
{
    static block_state_t& state(const pbf_block_t& block) { return *static_cast<block_state_t*>(block.state_); }
    static bool invoke(const descriptor_t& descriptor,
                       bool locations,
                       const pbf_block_handler_t& handler,
                       size_t worker,
                       std::atomic<bool>* stopped)
    {
        block_state_t state{descriptor, locations, std::this_thread::get_id(), stopped};
        const pbf_block_t block(&state);
        const callback_scope_t scope(worker, descriptor.index);
        return handler(block) && state.active();
    }
    static bool legacy(const pbf_block_t& block,
                       bool metadata,
                       const std::function<bool(std::span<const node_t>)>& nodes,
                       const std::function<bool(std::span<const way_t>)>& ways,
                       const std::function<bool(std::span<const relation_t>)>& relations)
    {
        auto& state = pbf_access_t::state(block);
        return state.run([&] {
            auto& decoder = state.prepare();
            decoder.metadata = metadata;
            decoder.ensure_strings();
            for (auto group : decoder.groups)
            {
                if (!state.active()) return false;
                decoder.storage.clear();
                const bool has_nodes = decoder.group(group);
                decoder.storage.finish();
                if (has_nodes && nodes && !nodes(decoder.storage.nodes)) return false;
                if (ways && !ways(decoder.storage.ways)) return false;
                if (relations && !relations(decoder.storage.relations)) return false;
            }
            return true;
        });
    }
};

size_t pbf_block_t::index() const noexcept
{
    return pbf_access_t::state(*this).descriptor.index;
}
uint64_t pbf_block_t::file_offset() const noexcept
{
    return pbf_access_t::state(*this).descriptor.offset;
}
bool pbf_block_t::parameters(pbf_parameters_t& result) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    pbf_parameters_t value;
    if (!state.run([&] {
            value = state.prepare().block;
            return true;
        }))
        return false;
    result = value;
    return true;
}
bool pbf_block_t::counts(pbf_counts_t& result) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    pbf_counts_t value;
    if (!state.run([&] {
            value = state.count(7);
            return true;
        }))
        return false;
    result = value;
    return true;
}
bool pbf_block_t::node_count(size_t& result) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    size_t value;
    if (!state.run([&] {
            value = state.count(1).nodes;
            return true;
        }))
        return false;
    result = value;
    return true;
}
bool pbf_block_t::way_count(size_t& result) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    size_t value;
    if (!state.run([&] {
            value = state.count(2).ways;
            return true;
        }))
        return false;
    result = value;
    return true;
}
bool pbf_block_t::relation_count(size_t& result) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    size_t value;
    if (!state.run([&] {
            value = state.count(4).relations;
            return true;
        }))
        return false;
    result = value;
    return true;
}
bool pbf_block_t::string_table_size(size_t& result) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    size_t value = 0;
    if (!state.run([&] {
            if (!state.string_count && !state.strings([](std::string_view) { return true; })) return false;
            value = *state.string_count;
            return true;
        }))
        return false;
    result = value;
    return true;
}
bool pbf_block_t::decode_strings(const pbf_string_handler_t& handler) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    return state.run([&] {
        require(bool(handler), "Empty PBF string handler");
        return state.strings(handler);
    });
}
bool pbf_block_t::decode_entities(pbf_entity_options_t options, const pbf_group_handler_t& handler) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    return state.run([&] {
        require(bool(handler), "Empty PBF entity handler");
        return state.decode(options, handler);
    });
}
bool pbf_block_t::decode_nodes(pbf_node_options_t options, const pbf_node_handler_t& handler) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    return state.run([&] {
        require(bool(handler), "Empty PBF node handler");
        return state.decode({options, {}, {}}, [&](const pbf_group_batch_t& batch) {
            return (batch.kind != pbf_group_kind_t::nodes && batch.kind != pbf_group_kind_t::dense_nodes) ||
                   handler(batch.nodes);
        });
    });
}
bool pbf_block_t::decode_ways(pbf_way_options_t options, const pbf_way_handler_t& handler) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    return state.run([&] {
        require(bool(handler), "Empty PBF way handler");
        return state.decode({{}, options, {}}, [&](const pbf_group_batch_t& batch) {
            return batch.kind != pbf_group_kind_t::ways || handler(batch.ways);
        });
    });
}
bool pbf_block_t::decode_relations(pbf_relation_options_t options, const pbf_relation_handler_t& handler) const noexcept
{
    auto& state = pbf_access_t::state(*this);
    return state.run([&] {
        require(bool(handler), "Empty PBF relation handler");
        return state.decode({{}, {}, options}, [&](const pbf_group_batch_t& batch) {
            return batch.kind != pbf_group_kind_t::relations || handler(batch.relations);
        });
    });
}
bool pbf_block_t::validate() const noexcept
{
    auto& state = pbf_access_t::state(*this);
    return state.run([&] {
        if (!state.strings([](std::string_view) { return true; })) return false;
        return state.decode({pbf_node_options_t{true, true, true, true, true},
                             pbf_way_options_t{true, true, true, true},
                             pbf_relation_options_t{true, true, true, true, true}},
                            [](const auto&) { return true; });
    });
}

namespace
{

struct context_t
{
    bool locations;
    const pbf_block_handler_t& handler;
    std::mutex mutex;
    std::condition_variable data_ready;
    std::condition_variable space_ready;
    std::vector<descriptor_t> queue;
    size_t head = 0;
    size_t tail = 0;
    size_t queued = 0;
    std::atomic<bool> stopped{false};
    bool finished = false;

    context_t(bool has_locations, const pbf_block_handler_t& block_handler, size_t count)
        : locations(has_locations),
          handler(block_handler),
          queue(count > 1 ? 2 * count : 0)
    {
    }
    void stop()
    {
        {
            std::lock_guard lock(mutex);
            stopped.store(true, std::memory_order_relaxed);
        }
        data_ready.notify_all();
        space_ready.notify_all();
    }
    bool active() const { return !stopped.load(std::memory_order_relaxed); }
    bool process(const descriptor_t& descriptor, size_t worker)
    {
        try
        {
            if (pbf_access_t::invoke(descriptor, locations, handler, worker, &stopped)) return true;
        }
        catch (const std::exception& error)
        {
            IOSM_ERROR("PBF block {} at offset {}: {}",
                       descriptor.index,
                       static_cast<unsigned long long>(descriptor.offset),
                       error.what());
        }
        catch (...)
        {
            IOSM_ERROR("PBF block {} at offset {}: callback exception",
                       descriptor.index,
                       static_cast<unsigned long long>(descriptor.offset));
        }
        stop();
        return false;
    }
    void worker(size_t index)
    {
        for (;;)
        {
            descriptor_t descriptor;
            {
                std::unique_lock lock(mutex);
                data_ready.wait(lock, [&] { return !active() || finished || queued != 0; });
                if (!active() || queued == 0) return;
                descriptor = queue[head];
                if (++head == queue.size()) head = 0;
                --queued;
            }
            space_ready.notify_one();
            if (!process(descriptor, index)) return;
        }
    }
};

bool read_all(bytes_t bytes, context_t& context, size_t count)
{
    reader_t reader(bytes);
    size_t index = 0;
    uint64_t offset = 0;
    auto descriptor = read_descriptor(reader, index++, offset);
    require(descriptor.type == "OSMHeader", "Missing initial OSMHeader");
    offset = static_cast<uint64_t>(descriptor.blob.data() - bytes.data()) + descriptor.blob.size();
    std::vector<std::thread> workers;
    try
    {
        if (count > 1)
        {
            workers.reserve(count);
            for (size_t i = 0; i < count; ++i) workers.emplace_back([&context, i] { context.worker(i); });
        }
        while (!reader.empty() && context.active())
        {
            descriptor = read_descriptor(reader, index++, offset);
            offset = static_cast<uint64_t>(descriptor.blob.data() - bytes.data()) + descriptor.blob.size();
            require(descriptor.type != "OSMHeader", "Unexpected second OSMHeader");
            if (descriptor.type != "OSMData") continue;
            if (count == 1)
            {
                if (!context.process(descriptor, 0)) break;
                continue;
            }
            std::unique_lock lock(context.mutex);
            context.space_ready.wait(lock, [&] { return !context.active() || context.queued < context.queue.size(); });
            if (!context.active()) break;
            context.queue[context.tail] = descriptor;
            if (++context.tail == context.queue.size()) context.tail = 0;
            ++context.queued;
            lock.unlock();
            context.data_ready.notify_one();
        }
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF file near block {} at offset {}: {}",
                   index - 1,
                   static_cast<unsigned long long>(offset),
                   error.what());
        context.stop();
    }
    catch (...)
    {
        IOSM_ERROR("PBF input failure");
        context.stop();
    }
    {
        std::lock_guard lock(context.mutex);
        context.finished = true;
        context.data_ready.notify_all();
    }
    for (auto& worker : workers) worker.join();
    return context.active();
}

size_t effective_threads(size_t count)
{
    const auto hardware = std::max<size_t>(1, std::thread::hardware_concurrency());
    return std::clamp<size_t>(count, 1, hardware);
}
} // namespace

struct pbf_reader_t::impl_t
{
    std::shared_ptr<mapping_t> mapping;
    std::vector<uint64_t> offsets;
    blob_decoder_t header_storage;
    bytes_t header_bytes;
    bool locations = false;
    std::atomic<bool> busy{false};
    std::vector<std::string_view> required_features, optional_features;

    explicit impl_t(const char* filename)
        : mapping(open_mapping(filename))
    {
        reader_t reader(mapping->bytes());
        const auto header = read_descriptor(reader, 0, 0);
        require(header.type == "OSMHeader", "Missing initial OSMHeader");
        header_bytes = blob_data(header.blob, header_storage);
        validate_header(header_bytes);
        reader_t fields(header_bytes);
        while (!fields.empty())
        {
            const auto field = fields.next();
            if (field.number == 5 && text(field.message()) == "LocationsOnWays") locations = true;
        }
    }
    struct operation_t
    {
        impl_t& impl;
        explicit operation_t(impl_t& value)
            : impl(value)
        {
            require(!impl.busy.exchange(true), "PBF reader already has an active operation");
        }
        ~operation_t() { impl.busy.store(false); }
    };
};

pbf_reader_t::pbf_reader_t() noexcept = default;
pbf_reader_t::~pbf_reader_t() = default;
pbf_reader_t::pbf_reader_t(pbf_reader_t&& other) noexcept
    : impl_(std::move(other.impl_)),
      configured_threads_(std::exchange(other.configured_threads_, 1))
{
}
pbf_reader_t& pbf_reader_t::operator=(pbf_reader_t&& other) noexcept
{
    if (this != &other)
    {
        impl_ = std::move(other.impl_);
        configured_threads_ = std::exchange(other.configured_threads_, 1);
    }
    return *this;
}

bool pbf_reader_t::open(const char* filename) noexcept
{
    try
    {
        require(!impl_, "PBF reader is already open");
        impl_ = std::make_unique<impl_t>(filename);
        return true;
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF open: {}", error.what());
    }
    catch (...)
    {
        IOSM_ERROR("PBF open failure");
    }
    return false;
}
void pbf_reader_t::close() noexcept
{
    if (impl_ && impl_->busy.load())
    {
        IOSM_ERROR("Cannot close an active PBF reader");
        return;
    }
    impl_.reset();
}
bool pbf_reader_t::is_open() const noexcept
{
    return bool(impl_);
}
void pbf_reader_t::set_thread_count(size_t count) noexcept
{
    configured_threads_ = effective_threads(count);
}
void pbf_reader_t::set_max_thread_count() noexcept
{
    set_thread_count(std::thread::hardware_concurrency());
}
size_t pbf_reader_t::thread_count() const noexcept
{
    return configured_threads_;
}
bool pbf_reader_t::has_index() const noexcept
{
    return impl_ && !impl_->offsets.empty();
}
size_t pbf_reader_t::index_memory_bytes() const noexcept
{
    return impl_ ? impl_->offsets.capacity() * sizeof(uint64_t) : 0;
}

bool pbf_reader_t::build_index() noexcept
{
    try
    {
        require(bool(impl_), "PBF reader is closed");
        impl_t::operation_t operation(*impl_);
        if (has_index()) return true;
        const auto bytes = impl_->mapping->bytes();
        reader_t reader(bytes);
        std::vector<uint64_t> offsets;
        offsets.reserve(std::min<size_t>(bytes.size() / 65536 + 1, 65536));
        uint64_t offset = 0;
        while (!reader.empty())
        {
            const auto descriptor = read_descriptor(reader, offsets.size(), offset);
            require(descriptor.type != "OSMHeader" || offsets.empty(), "Unexpected second OSMHeader");
            offsets.push_back(offset);
            offset = static_cast<uint64_t>(descriptor.blob.data() - bytes.data()) + descriptor.blob.size();
        }
        impl_->offsets = std::move(offsets);
        return true;
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF index: {}", error.what());
    }
    catch (...)
    {
        IOSM_ERROR("PBF index failure");
    }
    return false;
}

bool pbf_reader_t::decode_header(const pbf_header_handler_t& handler) noexcept
{
    try
    {
        require(bool(impl_), "PBF reader is closed");
        impl_t::operation_t operation(*impl_);
        require(bool(handler), "Empty PBF header handler");
        pbf_header_metadata_t result;
        impl_->required_features.clear();
        impl_->optional_features.clear();
        reader_t reader(impl_->header_bytes);
        unsigned bbox_fields = 0;
        while (!reader.empty())
        {
            const auto field = reader.next();
            switch (field.number)
            {
                case 1: {
                    if (!result.bbox) result.bbox.emplace();
                    reader_t bbox(field.message());
                    while (!bbox.empty())
                    {
                        const auto coordinate = bbox.next();
                        if (coordinate.number < 1 || coordinate.number > 4) continue;
                        const auto value = zigzag(coordinate.integer());
                        switch (coordinate.number)
                        {
                            case 1:
                                result.bbox->left = value;
                                break;
                            case 2:
                                result.bbox->right = value;
                                break;
                            case 3:
                                result.bbox->top = value;
                                break;
                            case 4:
                                result.bbox->bottom = value;
                                break;
                        }
                        bbox_fields |= 1u << (coordinate.number - 1);
                    }
                    break;
                }
                case 4:
                    impl_->required_features.push_back(text(field.message()));
                    break;
                case 5:
                    impl_->optional_features.push_back(text(field.message()));
                    break;
                case 16:
                    result.writingprogram = text(field.message());
                    break;
                case 17:
                    result.source = text(field.message());
                    break;
                case 32:
                    result.osmosis_replication_timestamp = signed_integer(field.integer());
                    break;
                case 33:
                    result.osmosis_replication_sequence_number = signed_integer(field.integer());
                    break;
                case 34:
                    result.osmosis_replication_base_url = text(field.message());
                    break;
                default:
                    break;
            }
        }
        require(!result.bbox || bbox_fields == 15, "Missing PBF bounding box coordinate");
        result.required_features = impl_->required_features;
        result.optional_features = impl_->optional_features;
        return handler(result);
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF header: {}", error.what());
    }
    catch (...)
    {
        IOSM_ERROR("PBF header callback failure");
    }
    return false;
}

bool pbf_reader_t::read_blocks(const pbf_block_handler_t& handler) noexcept
{
    try
    {
        require(bool(impl_), "PBF reader is closed");
        impl_t::operation_t operation(*impl_);
        require(bool(handler), "Empty PBF block handler");
        context_t context(impl_->locations, handler, thread_count());
        return read_all(impl_->mapping->bytes(), context, thread_count());
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF read: {}", error.what());
    }
    catch (...)
    {
        IOSM_ERROR("PBF read failure");
    }
    return false;
}

bool pbf_reader_t::read_block(size_t index, const pbf_block_handler_t& handler) noexcept
{
    uint64_t offset = 0;
    size_t scanned = 0;
    try
    {
        require(bool(impl_), "PBF reader is closed");
        impl_t::operation_t operation(*impl_);
        require(bool(handler), "Empty PBF block handler");
        const auto bytes = impl_->mapping->bytes();
        descriptor_t selected;
        if (has_index())
        {
            require(index < impl_->offsets.size(), "PBF block index outside file");
            offset = impl_->offsets[index];
            const auto end = index + 1 < impl_->offsets.size() ? impl_->offsets[index + 1] : bytes.size();
            reader_t reader(bytes.subspan(static_cast<size_t>(offset), static_cast<size_t>(end - offset)));
            selected = read_descriptor(reader, index, offset);
            require(reader.empty(), "Invalid indexed PBF block range");
        }
        else
        {
            reader_t reader(bytes);
            for (;; ++scanned)
            {
                require(!reader.empty(), "PBF block index outside file");
                selected = read_descriptor(reader, scanned, offset);
                require(selected.type != "OSMHeader" || scanned == 0, "Unexpected second OSMHeader");
                if (scanned == index) break;
                offset = static_cast<uint64_t>(selected.blob.data() - bytes.data()) + selected.blob.size();
            }
        }
        require(selected.type == "OSMData", "Requested PBF block is not OSMData");
        return pbf_access_t::invoke(selected, impl_->locations, handler, 0, nullptr);
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF request {} near block {} at offset {}: {}",
                   index,
                   has_index() ? index : scanned,
                   static_cast<unsigned long long>(offset),
                   error.what());
    }
    catch (...)
    {
        IOSM_ERROR("PBF request {} at offset {}: callback exception", index, static_cast<unsigned long long>(offset));
    }
    return false;
}

void set_thread_count(size_t count)
{
    configured_threads = effective_threads(count);
}
void set_max_thread_count()
{
    configured_threads = effective_threads(std::thread::hardware_concurrency());
}
size_t thread_count()
{
    return configured_threads ? configured_threads : 1;
}

bool input_pbf(const char* filename) noexcept
{
    file_type = file_type_t::pbf;
    osc_mode = mode_t::bulk;
    thread_index = 0;
    block_index = 0;
    try
    {
        const bool metadata = decode_metadata;
        const auto nodes = node_handler;
        const auto ways = way_handler;
        const auto relations = relation_handler;
        pbf_reader_t reader;
        reader.set_thread_count(thread_count());
        if (!reader.open(filename)) return false;
        return reader.read_blocks(
            [&](const pbf_block_t& block) { return pbf_access_t::legacy(block, metadata, nodes, ways, relations); });
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF input: {}", error.what());
    }
    catch (...)
    {
        IOSM_ERROR("PBF input failure");
    }
    return false;
}
} // namespace input_osm
