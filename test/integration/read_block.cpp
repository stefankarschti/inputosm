#include <inputosm/inputosm.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <fmt/format.h>

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4 || (argc == 4 && std::string_view(argv[3]) != "--index"))
    {
        std::fprintf(stderr, "Usage: read_block <file.osm.pbf> <block-index> [--index]\n");
        return EXIT_FAILURE;
    }
    try
    {
        const size_t index = std::stoull(argv[2]);
        input_osm::pbf_reader_t reader;
        if (!reader.open(argv[1])) return EXIT_FAILURE;
        if (argc == 4 && !reader.build_index()) return EXIT_FAILURE;
        const bool ok = reader.read_block(index, false, [](const input_osm::pbf_block_t& block) {
            fmt::print("block={} offset={} nodes={} ways={} relations={}\n",
                       block.index,
                       block.file_offset,
                       block.nodes.size(),
                       block.ways.size(),
                       block.relations.size());
            return true;
        });
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
}
