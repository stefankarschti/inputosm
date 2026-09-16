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
#include <bit>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include <zlib.h>

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
    bytes_t take(size_t count)
    {
        require(count <= remaining_.size(), "Truncated PBF field");
        auto result = remaining_.first(count);
        remaining_ = remaining_.subspan(count);
        return result;
    }
    [[gnu::always_inline]] inline uint64_t varint()
    {
        uint64_t result = 0;
        for (unsigned shift = 0; shift < 70; shift += 7)
        {
            const auto byte = take(1)[0];
            require(shift != 63 || byte <= 1, "PBF varint overflow");
            result |= uint64_t(byte & 0x7f) << shift;
            if (!(byte & 0x80)) return result;
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

struct storage_t
{
    std::vector<node_t> nodes;
    std::vector<way_t> ways;
    std::vector<relation_t> relations;
    std::vector<tag_t> tags;
    std::vector<int64_t> refs;
    std::vector<relation_member_t> members;
    std::vector<range_t> node_tags, way_tags, relation_tags, way_refs, relation_members;

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
        for (size_t i = 0; i < nodes.size(); ++i)
            nodes[i].tags = tag_span.subspan(node_tags[i].start, node_tags[i].size);
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

struct decoder_t
{
    bool metadata;
    std::vector<std::string_view> string_table;
    std::vector<bytes_t> groups;
    // The columns contain IDs, coordinates, tags, and the six DenseInfo fields.
    std::array<std::vector<field_t>, 10> dense_fields;
    std::vector<uint8_t> raw;
    storage_t storage;
    pbf_block_t block;

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
        storage.node_tags.push_back({start, keys});
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
    void collect_dense(bytes_t bytes)
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
        storage.node_tags.resize(storage.nodes.size());
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
                    storage.node_tags[node_index++] = {tag_start, storage.tags.size() - tag_start};
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
        unsigned kind = 0;
        bool has_nodes = false;
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            if (field.number >= 1 && field.number <= 4)
            {
                require(kind == 0 || kind == field.number, "Mixed entity types in a primitive group");
                kind = field.number;
            }
            switch (field.number)
            {
                case 1:
                    node(field.message());
                    has_nodes = true;
                    break;
                case 2:
                    collect_dense(field.message());
                    has_nodes = true;
                    break;
                case 3:
                    way(field.message());
                    break;
                case 4:
                    relation(field.message());
                    break;
                case 5:
                    throw std::runtime_error("Unsupported ChangeSet entity");
                default:
                    break;
            }
        }
        if (kind == 2) dense();
        return has_nodes;
    }
    void prepare(bytes_t bytes, size_t index, uint64_t offset)
    {
        storage.clear();
        string_table.clear();
        groups.clear();
        block = pbf_block_t{};
        block.index = index;
        block.file_offset = offset;
        bool has_table = false;
        reader_t reader(bytes);
        while (!reader.empty())
        {
            const auto field = reader.next();
            switch (field.number)
            {
                case 1: {
                    has_table = true;
                    reader_t strings(field.message());
                    while (!strings.empty())
                    {
                        const auto entry = strings.next();
                        if (entry.number == 1) string_table.push_back(text(entry.message()));
                    }
                    break;
                }
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
        require(has_table && !string_table.empty() && string_table[0].empty(), "Invalid or missing PBF string table");
        require(block.granularity > 0 && block.date_granularity > 0, "Invalid PBF granularity");
        block.string_table = string_table;
    }
};

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
    reader_t header(reader.take(size));
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

bytes_t blob_data(bytes_t blob, std::vector<uint8_t>& buffer)
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
    buffer.resize(std::max<size_t>(1, static_cast<size_t>(raw_size)));
    uLongf output_size = buffer.size();
    uLong input_size = payload.size();
    const int result = uncompress2(buffer.data(), &output_size, payload.data(), &input_size);
    require(result == Z_OK && output_size == static_cast<size_t>(raw_size) && input_size == payload.size(),
            "Invalid Zlib Blob data");
    return {buffer.data(), static_cast<size_t>(raw_size)};
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
    explicit mapping_t(const char* filename)
    {
        require(filename != nullptr, "Null PBF filename");
        const int fd = open(filename, O_RDONLY | O_CLOEXEC);
        require(fd >= 0, "Cannot open PBF file");
        struct stat status{};
        if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size <= 0 ||
            static_cast<uint64_t>(status.st_size) > static_cast<uint64_t>(std::numeric_limits<ptrdiff_t>::max()))
        {
            close(fd);
            throw std::runtime_error("Invalid PBF file size or type");
        }
        size_ = static_cast<size_t>(status.st_size);
        void* address = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        const int close_result = close(fd);
        require(address != MAP_FAILED, "Cannot map PBF file");
        data_ = static_cast<const uint8_t*>(address);
        if (close_result != 0)
        {
            release();
            throw std::runtime_error("Cannot close PBF file descriptor");
        }
    }
    ~mapping_t() { release(); }
    mapping_t(const mapping_t&) = delete;
    mapping_t& operator=(const mapping_t&) = delete;
    bytes_t bytes() const { return {data_, size_}; }
    bool release() noexcept
    {
        if (!data_) return true;
        const int result = munmap(const_cast<uint8_t*>(data_), size_);
        data_ = nullptr;
        if (result != 0) IOSM_ERROR("Cannot release PBF mapping");
        return result == 0;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

struct context_t
{
    bool metadata;
    pbf_block_handler_t block_handler;
    std::function<bool(std::span<const node_t>)> nodes;
    std::function<bool(std::span<const way_t>)> ways;
    std::function<bool(std::span<const relation_t>)> relations;
    std::mutex mutex;
    std::condition_variable changed;
    std::queue<descriptor_t> queue;
    bool stopped = false;
    bool finished = false;

    explicit context_t(bool read_metadata)
        : metadata(read_metadata)
    {
    }
    void stop()
    {
        std::lock_guard lock(mutex);
        stopped = true;
        changed.notify_all();
    }
    bool active()
    {
        std::lock_guard lock(mutex);
        return !stopped;
    }
    template <class Handler, class Data>
    bool invoke(const Handler& handler, const Data& data)
    {
        if (!active()) return false;
        if (!handler || handler(data)) return true;
        stop();
        return false;
    }
    bool process(const descriptor_t& descriptor, decoder_t& decoder)
    {
        block_index = descriptor.index;
        try
        {
            const auto bytes = blob_data(descriptor.blob, decoder.raw);
            decoder.prepare(bytes, descriptor.index, descriptor.offset);
            for (const auto group : decoder.groups)
            {
                if (!active()) return false;
                if (!block_handler) decoder.storage.clear();
                const bool has_nodes = decoder.group(group);
                if (block_handler) continue;
                decoder.storage.finish();
                if (has_nodes && !invoke(nodes, std::span<const node_t>(decoder.storage.nodes))) return false;
                if (!invoke(ways, std::span<const way_t>(decoder.storage.ways))) return false;
                if (!invoke(relations, std::span<const relation_t>(decoder.storage.relations))) return false;
            }
            if (!block_handler) return true;
            decoder.storage.finish();
            decoder.block.nodes = decoder.storage.nodes;
            decoder.block.ways = decoder.storage.ways;
            decoder.block.relations = decoder.storage.relations;
            return invoke(block_handler, decoder.block);
        }
        catch (const std::exception& error)
        {
            IOSM_ERROR("PBF block %zu at offset %llu: %s",
                       descriptor.index,
                       static_cast<unsigned long long>(descriptor.offset),
                       error.what());
        }
        catch (...)
        {
            IOSM_ERROR("PBF block %zu at offset %llu: callback exception",
                       descriptor.index,
                       static_cast<unsigned long long>(descriptor.offset));
        }
        stop();
        return false;
    }
    void worker(size_t index)
    {
        thread_index = index;
        decoder_t decoder(metadata);
        for (;;)
        {
            descriptor_t descriptor;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return stopped || finished || !queue.empty(); });
                if (stopped || queue.empty()) return;
                descriptor = queue.front();
                queue.pop();
                changed.notify_all();
            }
            if (!process(descriptor, decoder)) return;
        }
    }
};

bool read_file(const char* filename, context_t& context)
{
    mapping_t mapping(filename);
    reader_t reader(mapping.bytes());
    size_t index = 0;
    uint64_t offset = 0;
    auto descriptor = read_descriptor(reader, index++, offset);
    require(descriptor.type == "OSMHeader", "Missing initial OSMHeader");
    std::vector<uint8_t> header_buffer;
    validate_header(blob_data(descriptor.blob, header_buffer));
    offset = static_cast<uint64_t>(descriptor.blob.data() - mapping.bytes().data()) + descriptor.blob.size();
    const auto count = thread_count();
    std::vector<std::thread> workers;
    decoder_t decoder(context.metadata);
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
            offset = static_cast<uint64_t>(descriptor.blob.data() - mapping.bytes().data()) + descriptor.blob.size();
            require(descriptor.type != "OSMHeader", "Unexpected second OSMHeader");
            if (descriptor.type != "OSMData") continue;
            if (count == 1)
            {
                if (!context.process(descriptor, decoder)) break;
                continue;
            }
            std::unique_lock lock(context.mutex);
            context.changed.wait(lock, [&] { return context.stopped || context.queue.size() < 2 * count; });
            if (context.stopped) break;
            context.queue.push(descriptor);
            context.changed.notify_all();
        }
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF file near block %zu at offset %llu: %s",
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
        context.changed.notify_all();
    }
    for (auto& worker : workers) worker.join();
    const bool released = mapping.release();
    return context.active() && released;
}

bool run(const char* filename, context_t& context) noexcept
{
    thread_index = 0;
    block_index = 0;
    file_type = file_type_t::pbf;
    osc_mode = mode_t::bulk;
    try
    {
        return read_file(filename, context);
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF input: %s", error.what());
    }
    catch (...)
    {
        IOSM_ERROR("PBF input failure");
    }
    return false;
}
} // namespace

void set_thread_count(size_t count)
{
    configured_threads = std::min(count, static_cast<size_t>(std::thread::hardware_concurrency()));
}
void set_max_thread_count()
{
    configured_threads = std::thread::hardware_concurrency();
}
size_t thread_count()
{
    return configured_threads ? configured_threads : 1;
}

bool input_pbf_blocks(const char* filename, bool read_metadata, pbf_block_handler_t handler) noexcept
{
    if (!handler)
    {
        IOSM_ERROR("Empty PBF block handler");
        return false;
    }
    try
    {
        context_t context(read_metadata);
        context.block_handler = std::move(handler);
        return run(filename, context);
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF input: %s", error.what());
    }
    catch (...)
    {
        IOSM_ERROR("PBF input failure");
    }
    return false;
}

bool input_pbf(const char* filename) noexcept
{
    try
    {
        context_t context(decode_metadata);
        context.nodes = node_handler;
        context.ways = way_handler;
        context.relations = relation_handler;
        return run(filename, context);
    }
    catch (const std::exception& error)
    {
        IOSM_ERROR("PBF input: %s", error.what());
    }
    catch (...)
    {
        IOSM_ERROR("PBF input failure");
    }
    return false;
}
} // namespace input_osm
