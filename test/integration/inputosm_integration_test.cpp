#include <inputosm/inputosm.h>

#include <cstdio>
#include <cstdlib>
#include <atomic>

int main(int argc, char **argv)
{
    const char* path = "/mnt/maps/berlin-220123.osm.pbf";
    if (argc < 2)
    {
        printf("Usage %s <path-to-pbf>\n", argv[0]);
        // return EXIT_FAILURE;
    }
    else
        path = argv[1];

    std::atomic<uint64_t> node_count{0};
    std::atomic<uint64_t> way_count{0};
    std::atomic<uint64_t> relation_count{0};

    if (!input_osm::input_file(
            path,
            false,
            false,
            [&node_count](const input_osm::node_t &) -> bool
            { 
                // node_count++; 
                return true; 
            },
            [&way_count](const input_osm::way_t &) -> bool
            {
                // way_count++;
                return true;
            },
            [&relation_count](const input_osm::relation_t &) -> bool
            {
                // relation_count++;
                return true;
            }))
    {
        printf("Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    printf("%llu nodes\n", node_count.load());
    printf("%llu ways\n", way_count.load());
    printf("%llu relations\n", relation_count.load());

    return EXIT_SUCCESS;
}