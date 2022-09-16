#include <inputosm/inputosm.h>

#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <map>
#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage %s <path-to-pbf> [read-metadata]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    bool read_metadata = argc >= 2;
    
    std::vector<uint64_t> node_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> way_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> relation_count(input_osm::thread_count(), 0);

    if (!input_osm::input_file(
            path,
            read_metadata,
            [&node_count](input_osm::span_t<input_osm::node_t> node_list) -> bool
            { 
                node_count[input_osm::thread_index] += node_list.size();
                return true; 
            },
            [&way_count](input_osm::span_t<input_osm::way_t> way_list) -> bool
            {
                way_count[input_osm::thread_index] += way_list.size();
                return true;
            },
            [&relation_count](input_osm::span_t<input_osm::relation_t> relation_list) -> bool
            {
                relation_count[input_osm::thread_index] += relation_list.size();
                return true;
            }))
    {
        printf("Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    printf("%llu nodes\n", std::accumulate(node_count.begin(), node_count.end(), 0LLU));
    printf("%llu ways\n", std::accumulate(way_count.begin(), way_count.end(), 0LLU));
    printf("%llu relations\n", std::accumulate(relation_count.begin(), relation_count.end(), 0LLU));

    return EXIT_SUCCESS;
}