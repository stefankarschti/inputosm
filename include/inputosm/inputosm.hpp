// Copyright 2021-2026 Stefan Karschti
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

#pragma once

#include <span>
#include <string_view>
#include <cstddef>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace input_osm
{

struct tag_t
{
    std::string_view key;
    std::string_view value;
};

struct node_t
{
    int64_t id = 0;
    int64_t raw_latitude = 0;
    int64_t raw_longitude = 0;
    std::span<const tag_t> tags;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};
static_assert(sizeof(node_t) <= 64);

struct way_t
{
    int64_t id = 0;
    std::span<const int64_t> node_refs;
    std::span<const tag_t> tags;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};
static_assert(sizeof(way_t) <= 64);

struct relation_member_t
{
    /**
     * @brief Type of the relation member.
     * @details A node has type 0. A way has type 1. A relation has type 2.
     */
    uint8_t type = 0;
    int64_t id = 0;
    std::string_view role;
};

struct relation_t
{
    int64_t id = 0;
    std::span<const relation_member_t> members;
    std::span<const tag_t> tags;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
};
static_assert(sizeof(relation_t) <= 64);

struct pbf_parameters_t
{
    int32_t granularity = 100;
    int64_t lat_offset = 0;
    int64_t lon_offset = 0;
    int32_t date_granularity = 1000;
};

struct pbf_counts_t
{
    size_t nodes = 0, ways = 0, relations = 0;
};

struct pbf_header_bbox_t
{
    int64_t left = 0, right = 0, top = 0, bottom = 0;
};

struct pbf_header_metadata_t
{
    std::optional<pbf_header_bbox_t> bbox;
    std::span<const std::string_view> required_features, optional_features;
    std::optional<std::string_view> writingprogram, source;
    std::optional<int64_t> osmosis_replication_timestamp, osmosis_replication_sequence_number;
    std::optional<std::string_view> osmosis_replication_base_url;
};

struct pbf_node_options_t
{
    bool id = true, latitude = false, longitude = false, tags = false, metadata = false;
};
struct pbf_way_options_t
{
    bool tags = false, node_refs = false, metadata = false, node_locations = false;
};
struct pbf_relation_options_t
{
    bool tags = false, member_ids = false, member_types = false, member_roles = false, metadata = false;
};
struct pbf_entity_options_t
{
    std::optional<pbf_node_options_t> nodes;
    std::optional<pbf_way_options_t> ways;
    std::optional<pbf_relation_options_t> relations;
};

struct pbf_tag_ids_t
{
    uint32_t key = 0, value = 0;
};
struct pbf_location_t
{
    int64_t raw_latitude = 0, raw_longitude = 0;
};
enum class pbf_member_type_t : uint8_t
{
    node,
    way,
    relation
};
enum class pbf_group_kind_t : uint8_t
{
    empty,
    nodes,
    dense_nodes,
    ways,
    relations
};

struct pbf_metadata_t
{
    enum : uint8_t
    {
        version_present = 1,
        timestamp_present = 2,
        changeset_present = 4,
        uid_present = 8,
        user_present = 16,
        visible_present = 32
    };
    int64_t raw_timestamp = 0, changeset = 0;
    int32_t version = -1, uid = 0;
    uint32_t user_sid = 0;
    uint8_t present = 0;
    bool visible = true;
};

template <class T>
struct pbf_list_view_t
{
    std::span<const uint32_t> offsets;
    std::span<const T> values;
    std::span<const T> operator[](size_t index) const noexcept
    {
        return values.subspan(offsets[index], offsets[index + 1] - offsets[index]);
    }
};

struct pbf_batch_context_t
{
    size_t block_index = 0, group_index = 0, count = 0;
    pbf_parameters_t parameters;
};
struct pbf_node_batch_t : pbf_batch_context_t
{
    pbf_node_options_t fields;
    std::span<const int64_t> ids, raw_latitudes, raw_longitudes;
    pbf_list_view_t<pbf_tag_ids_t> tags;
    std::span<const pbf_metadata_t> metadata;
};
struct pbf_way_batch_t : pbf_batch_context_t
{
    pbf_way_options_t fields;
    std::span<const int64_t> ids;
    pbf_list_view_t<pbf_tag_ids_t> tags;
    pbf_list_view_t<int64_t> node_refs;
    pbf_list_view_t<pbf_location_t> node_locations;
    std::span<const uint8_t> locations_present;
    std::span<const pbf_metadata_t> metadata;
};
struct pbf_relation_batch_t : pbf_batch_context_t
{
    pbf_relation_options_t fields;
    std::span<const int64_t> ids;
    pbf_list_view_t<pbf_tag_ids_t> tags;
    std::span<const uint32_t> member_offsets, member_roles;
    std::span<const int64_t> member_ids;
    std::span<const pbf_member_type_t> member_types;
    std::span<const pbf_metadata_t> metadata;
};
struct pbf_group_batch_t
{
    size_t block_index = 0, group_index = 0;
    pbf_group_kind_t kind = pbf_group_kind_t::empty;
    pbf_node_batch_t nodes;
    pbf_way_batch_t ways;
    pbf_relation_batch_t relations;
};

using pbf_header_handler_t = std::function<bool(const pbf_header_metadata_t&)>;
using pbf_string_handler_t = std::function<bool(std::string_view)>;
using pbf_node_handler_t = std::function<bool(const pbf_node_batch_t&)>;
using pbf_way_handler_t = std::function<bool(const pbf_way_batch_t&)>;
using pbf_relation_handler_t = std::function<bool(const pbf_relation_batch_t&)>;
using pbf_group_handler_t = std::function<bool(const pbf_group_batch_t&)>;

/**
 * @brief Borrow a data block during its block callback.
 * @note Call methods in the callback thread. Do not retain the block reference.
 * @note Decode methods reuse group buffers after each entity callback.
 * @note String views remain valid until the block callback returns.
 * @note Applications must check string ID bounds before string lookup.
 */
class pbf_block_t
{
public:
    pbf_block_t(const pbf_block_t&) = delete;
    pbf_block_t& operator=(const pbf_block_t&) = delete;
    size_t index() const noexcept;
    uint64_t file_offset() const noexcept;
    bool parameters(pbf_parameters_t& result) const noexcept;
    bool string_table_size(size_t& result) const noexcept;
    bool decode_strings(const pbf_string_handler_t& handler) const noexcept;
    bool node_count(size_t& result) const noexcept;
    bool way_count(size_t& result) const noexcept;
    bool relation_count(size_t& result) const noexcept;
    bool counts(pbf_counts_t& result) const noexcept;
    bool decode_nodes(pbf_node_options_t options, const pbf_node_handler_t& handler) const noexcept;
    bool decode_ways(pbf_way_options_t options, const pbf_way_handler_t& handler) const noexcept;
    bool decode_relations(pbf_relation_options_t options, const pbf_relation_handler_t& handler) const noexcept;
    bool decode_entities(pbf_entity_options_t options, const pbf_group_handler_t& handler) const noexcept;
    bool validate() const noexcept;

private:
    friend struct pbf_access_t;
    explicit pbf_block_t(void* state) noexcept
        : state_(state)
    {
    }
    void* state_;
};
using pbf_block_handler_t = std::function<bool(const pbf_block_t&)>;

/**
 * @brief Read opaque PBF data blocks from an open file.
 * @note Separate readers can run concurrently. Use one operation at a time on each reader.
 * @note Keep the file contents unchanged until the reader closes.
 */
class pbf_reader_t
{
public:
    pbf_reader_t() noexcept;
    ~pbf_reader_t();
    pbf_reader_t(const pbf_reader_t&) = delete;
    pbf_reader_t& operator=(const pbf_reader_t&) = delete;
    pbf_reader_t(pbf_reader_t&& other) noexcept;
    pbf_reader_t& operator=(pbf_reader_t&& other) noexcept;

    bool open(const char* filename) noexcept;
    void close() noexcept;
    bool is_open() const noexcept;
    void set_thread_count(size_t count) noexcept;
    void set_max_thread_count() noexcept;
    size_t thread_count() const noexcept;
    bool build_index() noexcept;
    bool has_index() const noexcept;
    size_t index_memory_bytes() const noexcept;

    /** @brief Decode header metadata in the calling thread. Views last for its callback only. */
    bool decode_header(const pbf_header_handler_t& handler) noexcept;
    /** @brief Visit data blocks. Workers can call the handler concurrently and out of file order. */
    bool read_blocks(const pbf_block_handler_t& handler) noexcept;
    /** @brief Visit one data block in the calling thread. The header has file index zero. */
    bool read_block(size_t index, const pbf_block_handler_t& handler) noexcept;

private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
    size_t configured_threads_ = 1;
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

void set_verbose(bool value);

/** @brief Read entity batches. Entity handlers receive only nonempty batches. */
bool input_file(const char* filename,
                bool decode_metadata,
                std::function<bool(std::span<const node_t>)> node_handler,
                std::function<bool(std::span<const way_t>)> way_handler,
                std::function<bool(std::span<const relation_t>)> relation_handler) noexcept;

void set_thread_count(size_t);

void set_max_thread_count();

size_t thread_count();

enum log_level_t : uint8_t
{
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_INFO = 4,
    LOG_LEVEL_ERROR = 7,
    LOG_LEVEL_DISABLED = 255,
};

/**
 * @brief Set the log level.
 * @note This function is not thread-safe.
 */
void set_log_level(log_level_t) noexcept;

/**
 * @brief Callback that sends log messages to the user.
 * @note The message is a C string that ends with \0.
 */
using log_callback_t = void (*)(log_level_t level, const char* message);

/**
 * @brief Set the log callback.
 * @param log_callback The new log callback.
 * @note Multiple threads can call the callback at the same time. The callback must be thread-safe.
 * @return true if the function sets the callback. A null callback causes the function to return false.
 */
bool set_log_callback(log_callback_t log_callback) noexcept;

extern thread_local size_t thread_index;
extern thread_local size_t block_index;
extern mode_t osc_mode;
extern file_type_t file_type;

} // namespace input_osm
