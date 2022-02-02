#include <inputosm/inputosm.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage %s <path-to-pbf>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (!input_osm::input_file(
            argv[1],
            false,
            false,
            [](const input_osm::node_t &) -> bool
            { return true; },
            [](const input_osm::way_t &) -> bool
            { return true; },
            [](const input_osm::relation_t &) -> bool
            { return true; }))
    {
        printf("Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}