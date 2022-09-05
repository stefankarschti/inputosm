#include <inputosm/inputosm.h>

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>

int main(int argc, char **argv)
{
    // const char* path = "/mnt/maps/berlin-220123.osm.pbf";
    const char* path = "/mnt/maps/germany-220128.osm.pbf";
    if (argc < 2)
    {
        printf("Usage %s <path-to-pbf>\n", argv[0]);
        // return EXIT_FAILURE;
    }
    else
        path = argv[1];

    int k_num_threads = std::thread::hardware_concurrency();
    std::vector<uint64_t> node_count(k_num_threads, 0);
    std::vector<uint64_t> way_count(k_num_threads, 0);
    std::vector<uint64_t> relation_count(k_num_threads, 0);

    if (!input_osm::input_file(
            path,
            false,
            false,
            [&node_count](const input_osm::node_t &) -> bool
            { 
                assert(input_osm::thread_index >= 0 && input_osm::thread_index < std::thread::hardware_concurrency());
                node_count[input_osm::thread_index]++;
                return true; 
            },
            [&way_count](const input_osm::way_t &) -> bool
            {
                assert(input_osm::thread_index >= 0 && input_osm::thread_index < std::thread::hardware_concurrency());
                way_count[input_osm::thread_index]++;
                return true;
            },
            [&relation_count](const input_osm::relation_t &) -> bool
            {
                assert(input_osm::thread_index >= 0 && input_osm::thread_index < std::thread::hardware_concurrency());
                relation_count[input_osm::thread_index]++;
                return true;
            }))
    {
        printf("Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    uint64_t sum_node_count = 0;
    for(auto& x: node_count) sum_node_count += x;
    uint64_t sum_way_count = 0;
    for(auto& x: way_count) sum_way_count += x;
    uint64_t sum_relation_count = 0;
    for(auto& x: relation_count) sum_relation_count += x;

    printf("%llu nodes\n", sum_node_count);
    printf("%llu ways\n", sum_way_count);
    printf("%llu relations\n", sum_relation_count);

    return EXIT_SUCCESS;
}