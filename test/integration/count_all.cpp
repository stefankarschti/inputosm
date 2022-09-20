#include <inputosm/inputosm.h>

#include <iostream>
#include <cstdint>
#include <numeric>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage" << argv[0] << "<path-to-pbf> [read-metadata]\n";
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    std::cout << path << "\n";
    bool read_metadata = (argc >= 3);
    if(read_metadata)
        std::cout << "reading metadata\n";
    input_osm::set_max_thread_count();
    std::cout << "running on " << input_osm::thread_count() << " threads\n";

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
        std::cerr << "Error while processing pbf\n";
        return EXIT_FAILURE;
    }

    std::cout.imbue(std::locale(""));
    std::cout << "nodes: " << std::accumulate(node_count.begin(), node_count.end(), 0LLU) << "\n";
    std::cout << "ways: " << std::accumulate(way_count.begin(), way_count.end(), 0LLU) << "\n";
    std::cout << "relations: " << std::accumulate(relation_count.begin(), relation_count.end(), 0LLU) << "\n";

    return EXIT_SUCCESS;
}