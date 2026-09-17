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
#include <fmt/format.h>

#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <unordered_map>
#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fmt::print("Usage {} <path-to-pbf>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *path = argv[1];

    input_osm::set_max_thread_count();
    fmt::print("running on {} threads\n", fmt::group_digits(input_osm::thread_count()));

    std::vector<uint64_t> ferry_count(input_osm::thread_count(), 0);
    struct ferry_info
    {
        int64_t way_id;
        std::vector<int64_t> node_id;
    };
    std::vector<std::vector<ferry_info>> ferry(input_osm::thread_count());

    input_osm::pbf_reader_t reader;
    reader.set_thread_count(input_osm::thread_count());
    const bool ok = reader.open(path) && reader.read_blocks([&](const input_osm::pbf_block_t &block) {
        size_t ways = 0;
        if (!block.way_count(ways)) return false;
        if (ways == 0) return true;
        thread_local std::vector<uint8_t> strings;
        strings.clear();
        if (!block.decode_strings([&](std::string_view value) {
                strings.push_back(uint8_t(value == "route") | (uint8_t(value == "ferry") << 1));
                return true;
            }))
            return false;
        return block.decode_ways({true, true, false, false}, [&](const input_osm::pbf_way_batch_t &batch) {
            for (size_t i = 0; i < batch.count; ++i)
            {
                for (const auto tag : batch.tags[i])
                {
                    if (tag.key >= strings.size() || tag.value >= strings.size()) return false;
                    if ((strings[tag.key] & 1) && (strings[tag.value] & 2))
                    {
                        ++ferry_count[input_osm::thread_index];
                        const auto refs = batch.node_refs[i];
                        ferry[input_osm::thread_index].push_back({batch.ids[i], {refs.begin(), refs.end()}});
                    }
                }
            }
            return true;
        });
    });
    if (!ok)
    {
        fmt::print("Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    fmt::print("{} ferries\n", fmt::group_digits(std::accumulate(ferry_count.begin(), ferry_count.end(), 0LLU)));
    struct pos
    {
        int64_t raw_longitude;
        int64_t raw_latitude;
    };
    std::unordered_map<int64_t, pos> node_coord;
    for (auto &fv : ferry)
    {
        for (auto &f : fv)
        {
            for (auto &nid : f.node_id)
            {
                node_coord[nid] = pos{0, 0};
            }
        }
    }
    fmt::print("{} unique nodes used by ferries\n", fmt::group_digits(node_coord.size()));
    fmt::print("retrieving ferry node coordinates...\n");
    // The input must contain one node record for each node ID.
    // Each worker updates different map elements.
    if (!reader.read_blocks([&](const input_osm::pbf_block_t &block) {
            return block.decode_nodes({true, true, true, false, false}, [&](const input_osm::pbf_node_batch_t &batch) {
                for (size_t i = 0; i < batch.count; ++i)
                {
                    const auto it = node_coord.find(batch.ids[i]);
                    if (it != node_coord.end())
                    {
                        it->second = {batch.raw_longitudes[i], batch.raw_latitudes[i]};
                    }
                }
                return true;
            });
        }))
    {
        fmt::print("Error while processing pbf\n");
        return EXIT_FAILURE;
    }
    fmt::print("done.\n");

    return EXIT_SUCCESS;
}
