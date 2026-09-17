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

#include "counter.h"

#include <inputosm/inputosm.h>

#include <fmt/format.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <vector>
#include <span>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fmt::print(stderr, "Usage: count_entity <file.osm.pbf>\n");
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    fmt::print("{}\n", path);

    input_osm::pbf_reader_t reader;
    reader.set_max_thread_count();
    if (!reader.open(path)) return EXIT_FAILURE;

    const size_t actual_thread_count = reader.thread_count();

    fmt::print("running on {} threads\n", fmt::group_digits(actual_thread_count));

    // Allocate memory for all counters in one operation.
    std::vector<input_osm::Counter<uint64_t>> all_counters(3 * actual_thread_count);

    // Use a different span for each entity type.
    std::span<input_osm::Counter<uint64_t>> node_count(all_counters.data(), actual_thread_count);
    std::span<input_osm::Counter<uint64_t>> way_count(all_counters.data() + actual_thread_count, actual_thread_count);
    std::span<input_osm::Counter<uint64_t>> relation_count(all_counters.data() + 2 * actual_thread_count,
                                                           actual_thread_count);

    if (!reader.read_blocks([&](const input_osm::pbf_block_t& block) {
            input_osm::pbf_counts_t counts;
            if (!block.counts(counts)) return false;
            node_count[input_osm::thread_index] += counts.nodes;
            way_count[input_osm::thread_index] += counts.ways;
            relation_count[input_osm::thread_index] += counts.relations;
            return true;
        }))
    {
        fmt::print(stderr, "Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    fmt::print("nodes: {}\n", fmt::group_digits(std::accumulate(node_count.begin(), node_count.end(), 0LLU)));
    fmt::print("ways: {}\n", fmt::group_digits(std::accumulate(way_count.begin(), way_count.end(), 0LLU)));
    fmt::print("relations: {}\n",
               fmt::group_digits(std::accumulate(relation_count.begin(), relation_count.end(), 0LLU)));

    return EXIT_SUCCESS;
}
