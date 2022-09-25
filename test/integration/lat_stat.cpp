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

#include <iostream>
#include <iomanip>
#include <cstdint>
#include <numeric>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage" << argv[0] << "<path-to-pbf>\n";
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    std::cout << path << "\n";
    bool read_metadata = false;
    input_osm::set_max_thread_count();
    std::cout << "running on " << input_osm::thread_count() << " threads\n";

    std::vector<std::vector<int64_t>> node_count_by_lat(input_osm::thread_count(), std::vector<int64_t>(91, 0));

    if (!input_osm::input_file(
            path,
            read_metadata,
            [&node_count_by_lat](input_osm::span_t<input_osm::node_t> node_list) -> bool
            {
                for(auto &n: node_list)
                {
                    node_count_by_lat[input_osm::thread_index][std::abs(n.raw_latitude / 1e7)]++;
                }
                return true; 
            },
            nullptr,
            nullptr))
    {
        std::cerr << "Error while processing pbf\n";
        return EXIT_FAILURE;
    }

    // aggregate
    std::vector<int64_t> lats(91, 0);
    int64_t sum = 0;
    for(auto &ncbl: node_count_by_lat)
    {
        for(int degree = 0; degree <= 90; ++degree)
        {
            lats[degree] += ncbl[degree];
            sum += ncbl[degree];            
        }
    }

    std::cout.imbue(std::locale(""));
    std::cout << "|   degree |      count    |   percent  |\n";
    std::cout << "| -------- | ------------- | ---------- |\n";
    std::cout << std::fixed << std::setprecision(2);

    for(size_t i = 0; i <= 90; ++i)
    {
        std::cout << "| " << std::setw(8) << i << " | " << std::setw(13) << lats[i] << " | " << std::setw(9) << (lats[i] * 100.0 / sum) << "% |\n";
    }

    std::cout << "|   total  | " << std::setw(13) << sum << " |    100.00% |\n";
    return EXIT_SUCCESS;
}