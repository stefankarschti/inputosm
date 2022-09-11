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
        printf("Usage %s <path-to-pbf>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    
    std::vector<uint64_t> node_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> way_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> relation_count(input_osm::thread_count(), 0);

    std::vector<uint64_t> ferry_count(input_osm::thread_count(), 0);    
    struct ferry_info
    {
        int64_t way_id;
        std::vector<int64_t> node_id;
    };
    std::vector<std::vector<ferry_info>> ferry(input_osm::thread_count());

    if (!input_osm::input_file(
            path,
            false,
            false,
            [&node_count](input_osm::span_t<input_osm::node_t> node_list) -> bool
            { 
                node_count[input_osm::thread_index] += node_list.size();
                return true; 
            },
            [&way_count, &ferry_count, &ferry](input_osm::span_t<input_osm::way_t> way_list) -> bool
            {
                way_count[input_osm::thread_index] += way_list.size();
                // for(auto &way: way_list)
                // {
                //     for(auto &tag: way.tags)
                //     {
                //         if(strcasecmp(tag.key, "route") == 0 && strcasecmp(tag.value, "ferry") == 0)
                //         {
                //             ferry_count[input_osm::thread_index]++;
                //             ferry[input_osm::thread_index].emplace_back(ferry_info{.way_id = way.id, .node_id = std::vector<int64_t>(way.node_refs.begin(), way.node_refs.end())});
                //         }
                //     }
                // }
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

    printf("%llu ferries\n", std::accumulate(ferry_count.begin(), ferry_count.end(), 0));
    /*
    struct pos
    {
        int64_t raw_longitude;
        int64_t raw_latitude;
    };
    std::map<int64_t, pos> node_coord;
    for(auto &fv: ferry)
    {
        for(auto &f: fv)
        {
            for(auto &nid: f.node_id)
            {
                node_coord[nid] = pos{0,0};
            }
        }
    }
    printf("%llu unique nodes used by ferries\n", node_coord.size());
    printf("retrieving ferry node coordinates...\n");
    if (!input_osm::input_file(
            path,
            false,
            false,
            [&node_coord](input_osm::span_t<input_osm::node_t> node_list) -> bool
            { 
                assert(input_osm::thread_index >= 0 && input_osm::thread_index < std::thread::hardware_concurrency());
                for(auto &node: node_list)
                {
                    auto it = node_coord.find(node.id);
                    if(node_coord.end() != it)
                    {
                        it->second = {.raw_longitude = node.raw_latitude, .raw_latitude = node.raw_latitude};
                    }
                }
                return true; 
            },
            nullptr,
            nullptr))
    {
        printf("Error while processing pbf\n");
        return EXIT_FAILURE;
    }
    printf("done.\n");
    */

    return EXIT_SUCCESS;
}