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

#include <inputosm/inputosm.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <limits>
#include <utility>
#include <vector>
#include <fmt/format.h>
#include <fmt/chrono.h>

namespace
{
struct block_totals_t
{
    uint64_t count = 0, tags = 0, references = 0, with_tags = 0;
    int64_t max_id = 0, max_raw_timestamp = 0;

    template <class Batch>
    void add(const Batch& batch)
    {
        count += batch.count;
        tags += batch.tags.values.size();
        for (size_t i = 0; i < batch.count; ++i)
        {
            max_id = std::max(max_id, batch.ids[i]);
            with_tags += batch.tags.offsets[i] != batch.tags.offsets[i + 1];
        }
        for (const auto& metadata : batch.metadata)
            if (metadata.present & input_osm::pbf_metadata_t::timestamp_present)
                max_raw_timestamp = std::max(max_raw_timestamp, metadata.raw_timestamp);
    }
};

struct entity_statistics_t
{
    uint64_t count = 0, with_tags = 0, max_count = 0, max_tags = 0, max_references = 0;
    int64_t max_id = 0, max_timestamp = 0;

    bool add(const block_totals_t& block, int32_t date_granularity)
    {
        // Convert the nonnegative maximum to seconds without a millisecond overflow.
        const auto fraction = (block.max_raw_timestamp % 1000) * date_granularity / 1000;
        const auto whole = block.max_raw_timestamp / 1000;
        if (whole > (std::numeric_limits<int64_t>::max() - fraction) / date_granularity) return false;
        const auto timestamp = whole * date_granularity + fraction;
        if (!std::in_range<time_t>(timestamp)) return false;
        count += block.count;
        with_tags += block.with_tags;
        max_count = std::max(max_count, block.count);
        max_tags = std::max(max_tags, block.tags);
        max_references = std::max(max_references, block.references);
        max_id = std::max(max_id, block.max_id);
        max_timestamp = std::max(max_timestamp, timestamp);
        return true;
    }
    void merge(const entity_statistics_t& other)
    {
        count += other.count;
        with_tags += other.with_tags;
        max_count = std::max(max_count, other.max_count);
        max_tags = std::max(max_tags, other.max_tags);
        max_references = std::max(max_references, other.max_references);
        max_id = std::max(max_id, other.max_id);
        max_timestamp = std::max(max_timestamp, other.max_timestamp);
    }
};

// Each worker has separate cache lines for its statistics.
struct alignas(64) statistics_t
{
    std::array<entity_statistics_t, 3> entities;
    size_t max_block_index = 0;
};
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        fmt::print(stderr, "Usage: statistics <file.osm.pbf> [read-metadata]\n");
        return EXIT_FAILURE;
    }
    try
    {
        const char* path = argv[1];
        const bool read_metadata = argc == 3;
        fmt::print("{}\n", path);
        if (read_metadata) fmt::print("reading metadata\n");
        input_osm::pbf_reader_t reader;
        reader.set_max_thread_count();
        if (!reader.open(path)) return EXIT_FAILURE;
        fmt::print("running on {} threads\n", fmt::group_digits(reader.thread_count()));
        std::vector<statistics_t> workers(reader.thread_count());
        const input_osm::pbf_entity_options_t options{
            input_osm::pbf_node_options_t{.id = true, .tags = true, .metadata = read_metadata},
            input_osm::pbf_way_options_t{.tags = true, .node_refs = true, .metadata = read_metadata},
            input_osm::pbf_relation_options_t{.tags = true, .member_types = true, .metadata = read_metadata}};
        if (!reader.read_blocks([&](const input_osm::pbf_block_t& block) {
                std::array<block_totals_t, 3> totals;
                input_osm::pbf_parameters_t parameters;
                if (!block.parameters(parameters)) return false;
                if (!block.decode_entities(options, [&](const input_osm::pbf_group_batch_t& group) {
                        switch (group.kind)
                        {
                            case input_osm::pbf_group_kind_t::nodes:
                            case input_osm::pbf_group_kind_t::dense_nodes:
                                totals[0].add(group.nodes);
                                break;
                            case input_osm::pbf_group_kind_t::ways:
                                totals[1].add(group.ways);
                                totals[1].references += group.ways.node_refs.values.size();
                                break;
                            case input_osm::pbf_group_kind_t::relations:
                                totals[2].add(group.relations);
                                totals[2].references += group.relations.member_types.size();
                                break;
                            case input_osm::pbf_group_kind_t::empty:
                                break;
                        }
                        return true;
                    }))
                    return false;
                auto& worker = workers[input_osm::thread_index];
                for (size_t i = 0; i < totals.size(); ++i)
                    if (!worker.entities[i].add(totals[i], parameters.date_granularity)) return false;
                worker.max_block_index = std::max(worker.max_block_index, block.index());
                return true;
            }))
        {
            fmt::print(stderr, "Error while processing pbf\n");
            return EXIT_FAILURE;
        }
        statistics_t result;
        for (const auto& worker : workers)
        {
            for (size_t i = 0; i < result.entities.size(); ++i) result.entities[i].merge(worker.entities[i]);
            result.max_block_index = std::max(result.max_block_index, worker.max_block_index);
        }
        constexpr std::array singular{"node", "way", "relation"};
        constexpr std::array plural{"nodes", "ways", "relations"};
        for (size_t i = 0; i < result.entities.size(); ++i)
            fmt::print("{}: {}\n", plural[i], fmt::group_digits(result.entities[i].count));
        for (size_t i = 0; i < result.entities.size(); ++i)
        {
            const auto& entity = result.entities[i];
            fmt::print("max {} per block: {}\n", plural[i], fmt::group_digits(entity.max_count));
            fmt::print("max {} tags per block: {}\n", singular[i], fmt::group_digits(entity.max_tags));
            if (i != 0)
                fmt::print("max {} {} per block: {}\n",
                           singular[i],
                           i == 1 ? "nodes" : "members",
                           fmt::group_digits(entity.max_references));
        }
        for (size_t i = 0; i < result.entities.size(); ++i)
            fmt::print("max {} timestamp: {:%F %T} GMT\n",
                       singular[i],
                       fmt::gmtime(static_cast<time_t>(result.entities[i].max_timestamp)));
        fmt::print("max file block index: {}\n", result.max_block_index);
        for (size_t i = 0; i < result.entities.size(); ++i)
            fmt::print("{} with tags: {}\n", plural[i], fmt::group_digits(result.entities[i].with_tags));
        for (size_t i = 0; i < result.entities.size(); ++i)
            fmt::print("max {} id: {}\n", singular[i], result.entities[i].max_id);
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        fmt::print(stderr, "Statistics failed: {}\n", error.what());
        return EXIT_FAILURE;
    }
}
