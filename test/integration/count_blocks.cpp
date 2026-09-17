#include "counter.h"
#include <inputosm/inputosm.h>

#include <cstdint>
#include <cstdlib>
#include <fmt/format.h>
#include <cstdio>
#include <numeric>

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

    // Allocate memory for all counters in one operation.
    const size_t actual_thread_count = reader.thread_count();
    std::vector<input_osm::Counter<uint64_t>> all_counters(4 * actual_thread_count);

    // Use a different span for each entity type.
    std::span<input_osm::Counter<uint64_t>> node_count(all_counters.data(), actual_thread_count);
    std::span<input_osm::Counter<uint64_t>> way_count(all_counters.data() + actual_thread_count, actual_thread_count);
    std::span<input_osm::Counter<uint64_t>> relation_count(all_counters.data() + 2 * actual_thread_count,
                                                           actual_thread_count);
    std::span<input_osm::Counter<uint64_t>> block_count(all_counters.data() + 3 * actual_thread_count,
                                                        actual_thread_count);

    const bool result = reader.read_blocks([&](const input_osm::pbf_block_t& block) {
        input_osm::pbf_counts_t counts;
        if (!block.counts(counts)) return false;
        node_count[input_osm::thread_index] += counts.nodes;
        way_count[input_osm::thread_index] += counts.ways;
        relation_count[input_osm::thread_index] += counts.relations;
        block_count[input_osm::thread_index] += 1;
        return true;
    });

    if (!result)
    {
        fmt::print(stderr, "Input did not complete\n");
        return EXIT_FAILURE;
    }

    fmt::print("nodes: {}\n", fmt::group_digits(std::accumulate(node_count.begin(), node_count.end(), 0LLU)));
    fmt::print("ways: {}\n", fmt::group_digits(std::accumulate(way_count.begin(), way_count.end(), 0LLU)));
    fmt::print("relations: {}\n",
               fmt::group_digits(std::accumulate(relation_count.begin(), relation_count.end(), 0LLU)));
    fmt::print("blocks: {}\n", fmt::group_digits(std::accumulate(block_count.begin(), block_count.end(), 0LLU)));

    return EXIT_SUCCESS;
}
