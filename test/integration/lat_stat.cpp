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

#include "counter.hpp"

#include <inputosm/inputosm.hpp>

#include <fmt/format.h>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fmt::print(stderr, "Usage{}<path-to-pbf>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    fmt::print("{}\n", path);
    input_osm::set_max_thread_count();
    fmt::print("running on {} threads\n", fmt::group_digits(input_osm::thread_count()));

    // Allocate memory for all counters in one operation.
    constexpr unsigned total_lat_degree_values = 91;
    std::vector<input_osm::u64_64B> node_count_by_lat(total_lat_degree_values * input_osm::thread_count(), 0);

    input_osm::pbf_reader_t reader;
    reader.set_thread_count(input_osm::thread_count());
    const bool ok = reader.open(path) && reader.read_blocks([&](const input_osm::pbf_block_t& block) {
        return block.decode_nodes({false, true, false, false, false}, [&](const input_osm::pbf_node_batch_t& batch) {
            for (const auto raw : batch.raw_latitudes)
            {
                const auto latitude =
                    (double(batch.parameters.lat_offset) + double(batch.parameters.granularity) * double(raw)) / 1e9;
                if (!std::isfinite(latitude) || std::abs(latitude) > 90) return false;
                const auto degree = static_cast<size_t>(std::abs(latitude));
                ++node_count_by_lat[input_osm::thread_index * total_lat_degree_values + degree];
            }
            return true;
        });
    });
    if (!ok)
    {
        fmt::print(stderr, "Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    // Add the counts from all threads.
    std::vector<int64_t> lats(total_lat_degree_values, 0);
    int64_t sum = 0;

    for (size_t ti = 0; ti < input_osm::thread_count(); ++ti)
    {
        for (unsigned int degree = 0; degree < total_lat_degree_values; ++degree)
        {
            lats[degree] += node_count_by_lat[ti * total_lat_degree_values + degree];
            sum += node_count_by_lat[ti * total_lat_degree_values + degree];
        }
    }

    const auto count_width = std::max<size_t>(13, fmt::formatted_size("{}", fmt::group_digits(sum)));
    fmt::print("|   degree | {:^{}} |   percent  |\n", "count", count_width);
    fmt::print("| -------- | {:-<{}} | ---------- |\n", "", count_width);

    for (size_t i = 0; i < total_lat_degree_values; ++i)
    {
        fmt::print("| {:8} | {:>{}} | {:9.2f}% |\n",
                   i,
                   fmt::group_digits(lats[i]),
                   count_width,
                   sum ? lats[i] * 100.0 / sum : 0.0);
    }

    fmt::print("|   total  | {:>{}} |    100.00% |\n", fmt::group_digits(sum), count_width);
    return EXIT_SUCCESS;
}
