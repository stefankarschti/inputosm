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

#include <iostream>
#include <cstdint>
#include <numeric>
#include <vector>
#include <span>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage" << argv[0] << "<path-to-pbf> [read-metadata]\n";
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    std::cout << path << "\n";
    bool read_metadata = (argc >= 3);
    if (read_metadata) std::cout << "reading metadata\n";
    input_osm::set_max_thread_count();

    const size_t actual_thread_count = input_osm::thread_count();

    std::cout << "running on " << actual_thread_count << " threads\n";

    // do a single allocation
    std::vector<input_osm::Counter<uint64_t>> all_counters(3 * actual_thread_count);

    // use spans to split counters for each entity
    std::span<input_osm::Counter<uint64_t>> node_count(all_counters.data(), actual_thread_count);
    std::span<input_osm::Counter<uint64_t>> way_count(all_counters.data() + actual_thread_count, actual_thread_count);
    std::span<input_osm::Counter<uint64_t>> relation_count(all_counters.data() + 2 * actual_thread_count,
                                                           actual_thread_count);

    if (!input_osm::input_file(
            path,
            read_metadata,
            [&node_count](input_osm::span_t<input_osm::node_t> node_list) -> bool {
                node_count[input_osm::thread_index] += node_list.size();
                return true;
            },
            [&way_count](input_osm::span_t<input_osm::way_t> way_list) -> bool {
                way_count[input_osm::thread_index] += way_list.size();
                return true;
            },
            [&relation_count](input_osm::span_t<input_osm::relation_t> relation_list) -> bool {
                relation_count[input_osm::thread_index] += relation_list.size();
                return true;
            }))
    {
        std::cerr << "Error while processing pbf\n";
        return EXIT_FAILURE;
    }

    std::cout.imbue(std::locale(""));
    std::cout << "nodes: " << std::accumulate(node_count.begin(), node_count.end(), 0LLU) << "\n";
    std::cout << "ways: " << std::accumulate(way_count.begin(), way_count.end(), 0LLU) << "\n";
    std::cout << "relations: " << std::accumulate(relation_count.begin(), relation_count.end(), 0LLU) << "\n";

    return EXIT_SUCCESS;
}