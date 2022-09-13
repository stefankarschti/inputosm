#include <inputosm/inputosm.h>

#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <map>
#include <cstring>
#include <vector>
#include <algorithm>

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

    std::vector<uint64_t> max_node_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> max_node_tag_count(input_osm::thread_count(), 0);

    std::vector<uint64_t> max_way_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> max_way_tag_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> max_way_node_count(input_osm::thread_count(), 0);

    std::vector<uint64_t> max_relation_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> max_relation_tag_count(input_osm::thread_count(), 0);
    std::vector<uint64_t> max_relation_member_count(input_osm::thread_count(), 0);

    if (!input_osm::input_file(
            path,
            false,
            [&node_count, &max_node_count, &max_node_tag_count](input_osm::span_t<input_osm::node_t> node_list) noexcept -> bool
            { 
                auto cnt = node_list.size();
                node_count[input_osm::thread_index] += cnt;
                if(cnt > max_node_count[input_osm::thread_index])
                    max_node_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &n: node_list) cnt += n.tags.size();
                if(cnt > max_node_tag_count[input_osm::thread_index])
                    max_node_tag_count[input_osm::thread_index] = cnt;
                return true; 
            },
            [&way_count, &max_way_count, &max_way_tag_count, &max_way_node_count](input_osm::span_t<input_osm::way_t> way_list) noexcept -> bool
            {
                auto cnt = way_list.size();
                way_count[input_osm::thread_index] += cnt;
                if(cnt > max_way_count[input_osm::thread_index])
                    max_way_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &w: way_list) cnt += w.tags.size();
                if(cnt > max_way_tag_count[input_osm::thread_index])
                    max_way_tag_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &w: way_list) cnt += w.node_refs.size();
                if(cnt > max_way_node_count[input_osm::thread_index])
                    max_way_node_count[input_osm::thread_index] = cnt;
                return true;
            },
            [&relation_count, &max_relation_count, &max_relation_tag_count, &max_relation_member_count](input_osm::span_t<input_osm::relation_t> relation_list) noexcept -> bool
            {
                auto cnt = relation_list.size();
                relation_count[input_osm::thread_index] += cnt;
                if(cnt > max_relation_count[input_osm::thread_index])
                    max_relation_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &r: relation_list) cnt += r.tags.size();
                if(cnt > max_relation_tag_count[input_osm::thread_index])
                    max_relation_tag_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &r: relation_list) cnt += r.members.size();
                if(cnt > max_relation_member_count[input_osm::thread_index])
                    max_relation_member_count[input_osm::thread_index] = cnt;

                return true;
            }))
    {
        printf("Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    printf("%llu nodes\n", std::accumulate(node_count.begin(), node_count.end(), 0LLU));
    printf("%llu ways\n", std::accumulate(way_count.begin(), way_count.end(), 0LLU));
    printf("%llu relations\n", std::accumulate(relation_count.begin(), relation_count.end(), 0LLU));

    printf("max nodes per block: %llu\n", *std::max_element(max_node_count.begin(), max_node_count.end()));
    printf("max node tags per block: %llu\n", *std::max_element(max_node_tag_count.begin(), max_node_tag_count.end()));

    printf("max ways per block: %llu\n", *std::max_element(max_way_count.begin(), max_way_count.end()));
    printf("max way tags per block: %llu\n", *std::max_element(max_way_tag_count.begin(), max_way_tag_count.end()));
    printf("max way nodes per block: %llu\n", *std::max_element(max_way_node_count.begin(), max_way_node_count.end()));

    printf("max relations per block: %llu\n", *std::max_element(max_relation_count.begin(), max_relation_count.end()));
    printf("max relation tags per block: %llu\n", *std::max_element(max_relation_tag_count.begin(), max_relation_tag_count.end()));
    printf("max relation members per block: %llu\n", *std::max_element(max_relation_member_count.begin(), max_relation_member_count.end()));
    return EXIT_SUCCESS;
}