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
#include <numeric>
#include <vector>
#include <span>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fmt::print(stderr, "Usage{}<path-to-pbf> [read-metadata]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    fmt::print("{}\n", path);
    bool read_metadata = (argc >= 3);
    if (read_metadata) fmt::print("reading metadata\n");
    input_osm::set_max_thread_count();

    const size_t actual_thread_count = input_osm::thread_count();

    fmt::print("running on {} threads\n", fmt::group_digits(actual_thread_count));

    // Allocate memory for all counters in one operation.
    std::vector<input_osm::Counter<uint64_t>> all_counters(3 * actual_thread_count);

    // Use a different span for each entity type.
    std::span<input_osm::Counter<uint64_t>> node_count(all_counters.data(), actual_thread_count);
    std::span<input_osm::Counter<uint64_t>> way_count(all_counters.data() + actual_thread_count, actual_thread_count);
    std::span<input_osm::Counter<uint64_t>> relation_count(all_counters.data() + 2 * actual_thread_count,
                                                           actual_thread_count);

    if (!input_osm::input_file(
            path,
            read_metadata,
            [&node_count](std::span<const input_osm::node_t> node_list) -> bool {
                node_count[input_osm::thread_index] += node_list.size();
                return true;
            },
            [&way_count](std::span<const input_osm::way_t> way_list) -> bool {
                way_count[input_osm::thread_index] += way_list.size();
                return true;
            },
            [&relation_count](std::span<const input_osm::relation_t> relation_list) -> bool {
                relation_count[input_osm::thread_index] += relation_list.size();
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