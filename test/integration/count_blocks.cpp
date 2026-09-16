#include <inputosm/inputosm.h>

#include <cstdint>
#include <cstdlib>
#include <fmt/format.h>
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fmt::print(stderr, "Usage: count_blocks <file.osm.pbf>\n");
        return EXIT_FAILURE;
    }

    input_osm::set_thread_count(1);
    uint64_t blocks = 0;
    uint64_t nodes = 0;
    uint64_t ways = 0;
    uint64_t relations = 0;

    const bool result = input_osm::input_pbf_blocks(argv[1], false, [&](const input_osm::pbf_block_t& block) {
        ++blocks;
        nodes += block.nodes.size();
        ways += block.ways.size();
        relations += block.relations.size();
        return true;
    });

    if (!result)
    {
        fmt::print(stderr, "Input did not complete\n");
        return EXIT_FAILURE;
    }

    fmt::print("blocks={} nodes={} ways={} relations={}\n",
               fmt::group_digits(blocks),
               fmt::group_digits(nodes),
               fmt::group_digits(ways),
               fmt::group_digits(relations));
    return EXIT_SUCCESS;
}
