#include <inputosm/inputosm.h>

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <numeric>

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

    std::vector<uint64_t> node_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> way_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> relation_count(input_osm::thread_count(), 0);

    if (!input_osm::input_file(
            path,
            false,
            false,
            [&node_count](input_osm::span_t<input_osm::node_t> node_list) -> bool
            { 
                assert(input_osm::thread_index >= 0 && input_osm::thread_index < std::thread::hardware_concurrency());
                node_count[input_osm::thread_index] += node_list.size();
                return true; 
            },
            [&way_count](const input_osm::way_t &) -> bool
            {
                assert(input_osm::thread_index() >= 0 && input_osm::thread_index() < std::thread::hardware_concurrency());
                way_count[input_osm::thread_index]++;
                return true;
            },
            [&relation_count](const input_osm::relation_t &) -> bool
            {
                assert(input_osm::thread_index() >= 0 && input_osm::thread_index() < std::thread::hardware_concurrency());
                relation_count[input_osm::thread_index]++;
                return true;
            }))
    {
        printf("Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    printf("%llu nodes\n", std::accumulate(node_count.begin(), node_count.end(), 0));
    printf("%llu ways\n", std::accumulate(way_count.begin(), way_count.end(), 0));
    printf("%llu relations\n", std::accumulate(relation_count.begin(), relation_count.end(), 0));

    return EXIT_SUCCESS;
}