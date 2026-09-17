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

#ifndef INPUTOSM_H
#define INPUTOSM_H

#include <span>
#include <string_view>
#include <cstddef>

#include <cstdint>
#include <functional>
#include <memory>

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

struct pbf_block_t
{
    size_t index = 0;
    uint64_t file_offset = 0;

    std::span<const std::string_view> string_table;
    std::span<const node_t> nodes;
    std::span<const way_t> ways;
    std::span<const relation_t> relations;

    int32_t granularity = 100;
    int64_t lat_offset = 0;
    int64_t lon_offset = 0;
    int32_t date_granularity = 1000;
};

using pbf_block_handler_t = std::function<bool(const pbf_block_t&)>;

/**
 * @brief Read complete PBF blocks from an open file.
 * @note Separate readers can run concurrently. Use one operation at a time on each reader.
 * @note Keep the file contents unchanged until the reader closes.
 * @note All block views remain valid only during their callback.
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

    /** @brief Build an optional offset table for repeated indexed reads. */
    bool build_index() noexcept;
    bool has_index() const noexcept;
    size_t index_memory_bytes() const noexcept;

    /**
     * @brief Read all data blocks from the file start.
     * @note Multiple workers can call the handler concurrently and out of file order.
     * @return true on completion. A stop request or error produces false.
     */
    bool read_blocks(bool decode_metadata, const pbf_block_handler_t& handler) noexcept;

    /**
     * @brief Read one data block by its file block index.
     * @note Without an offset table, each call scans file headers from the file start.
     * @note The initial header has index zero. Unknown block types also occupy index positions.
     * @note The callback runs in the calling thread.
     * @return true if the handler returns true. An unavailable data block or error produces false.
     */
    bool read_block(size_t index, bool decode_metadata, const pbf_block_handler_t& handler) noexcept;

private:
    friend bool input_pbf(const char* filename) noexcept;
    bool read_blocks(bool decode_metadata,
                     const std::function<bool(const pbf_block_t&, bool)>& handler,
                     bool group_batches) noexcept;
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

#endif // !INPUTOSM_H
