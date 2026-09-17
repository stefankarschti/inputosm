#include "counter.h"
#include <inputosm/inputosm.h>

#include <cstdint>
#include <cstdlib>
#include <fmt/format.h>
#include <cstdio>
#include <numeric>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fmt::print(stderr, "Usage: count_blocks <file.osm.pbf>\n");
        return EXIT_FAILURE;
    }

    input_osm::pbf_reader_t reader;
    reader.set_max_thread_count();
    if (!reader.open(argv[1])) return EXIT_FAILURE;

    // Use one counter for each worker thread.
    std::vector<input_osm::Counter<uint64_t>> block_count(reader.thread_count());

    const bool result = reader.read_blocks([&](const input_osm::pbf_block_t&) {
        block_count[input_osm::thread_index] += 1;
        return true;
    });

    if (!result)
    {
        fmt::print(stderr, "Input did not complete\n");
        return EXIT_FAILURE;
    }

    fmt::print("blocks: {}\n", fmt::group_digits(std::accumulate(block_count.begin(), block_count.end(), uint64_t{0})));

    return EXIT_SUCCESS;
}
