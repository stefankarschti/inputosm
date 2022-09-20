#include <inputosm/inputosm.h>

#include <iostream>
#include <cstdint>
#include <numeric>
#include <vector>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <iomanip>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage" << argv[0] << "<path-to-pbf> [read-metadata]\n";
        return EXIT_FAILURE;
    }
    const char* path = argv[1];
    printf("%s\n", path);
    bool read_metadata = (argc >= 3);
    if(read_metadata)
        std::cout << "reading metadata\n";
    input_osm::set_max_thread_count();
    std::cout << "running on " << input_osm::thread_count() << " threads\n";
    
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

    std::vector<int32_t> node_timestamp(input_osm::thread_count(), 0);
    std::vector<int32_t> way_timestamp(input_osm::thread_count(), 0);
    std::vector<int32_t> relation_timestamp(input_osm::thread_count(), 0);

    std::vector<size_t> block_index(input_osm::thread_count(), 0);

    if (!input_osm::input_file(
            path,
            read_metadata,
            [&node_count, &max_node_count, &max_node_tag_count, &node_timestamp, &block_index](input_osm::span_t<input_osm::node_t> node_list) noexcept -> bool
            { 
                auto cnt = node_list.size();
                node_count[input_osm::thread_index] += cnt;
                if(cnt > max_node_count[input_osm::thread_index])
                    max_node_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &n: node_list) 
                    cnt += n.tags.size();
                if(cnt > max_node_tag_count[input_osm::thread_index])
                    max_node_tag_count[input_osm::thread_index] = cnt;
                for(auto &n: node_list)
                    node_timestamp[input_osm::thread_index] = std::max(node_timestamp[input_osm::thread_index], n.timestamp);
                block_index[input_osm::thread_index] = std::max(block_index[input_osm::thread_index], input_osm::block_index);
                return true; 
            },
            [&way_count, &max_way_count, &max_way_tag_count, &max_way_node_count, &way_timestamp, &block_index](input_osm::span_t<input_osm::way_t> way_list) noexcept -> bool
            {
                auto cnt = way_list.size();
                way_count[input_osm::thread_index] += cnt;
                if(cnt > max_way_count[input_osm::thread_index])
                    max_way_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &w: way_list)
                    cnt += w.tags.size();
                if(cnt > max_way_tag_count[input_osm::thread_index])
                    max_way_tag_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &w: way_list)
                    cnt += w.node_refs.size();
                if(cnt > max_way_node_count[input_osm::thread_index])
                    max_way_node_count[input_osm::thread_index] = cnt;
                for(auto &w: way_list)
                    way_timestamp[input_osm::thread_index] = std::max(way_timestamp[input_osm::thread_index], w.timestamp);
                block_index[input_osm::thread_index] = std::max(block_index[input_osm::thread_index], input_osm::block_index);
                return true;
            },
            [&relation_count, &max_relation_count, &max_relation_tag_count, &max_relation_member_count, &relation_timestamp, &block_index](input_osm::span_t<input_osm::relation_t> relation_list) noexcept -> bool
            {
                auto cnt = relation_list.size();
                relation_count[input_osm::thread_index] += cnt;
                if(cnt > max_relation_count[input_osm::thread_index])
                    max_relation_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &r: relation_list) 
                    cnt += r.tags.size();
                if(cnt > max_relation_tag_count[input_osm::thread_index])
                    max_relation_tag_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for(auto &r: relation_list)
                    cnt += r.members.size();
                if(cnt > max_relation_member_count[input_osm::thread_index])
                    max_relation_member_count[input_osm::thread_index] = cnt;
                for(auto &r: relation_list)
                    relation_timestamp[input_osm::thread_index] = std::max(relation_timestamp[input_osm::thread_index], r.timestamp);
                block_index[input_osm::thread_index] = std::max(block_index[input_osm::thread_index], input_osm::block_index);
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

    std::cout << "max nodes per block: " << *std::max_element(max_node_count.begin(), max_node_count.end()) << "\n";
    std::cout << "max node tags per block: " << *std::max_element(max_node_tag_count.begin(), max_node_tag_count.end()) << "\n";

    std::cout << "max ways per block: " << *std::max_element(max_way_count.begin(), max_way_count.end()) << "\n";
    std::cout << "max way tags per block: " << *std::max_element(max_way_tag_count.begin(), max_way_tag_count.end()) << "\n";
    std::cout << "max way nodes per block: " << *std::max_element(max_way_node_count.begin(), max_way_node_count.end()) << "\n";

    std::cout << "max relations per block: " << *std::max_element(max_relation_count.begin(), max_relation_count.end()) << "\n";
    std::cout << "max relation tags per block: " << *std::max_element(max_relation_tag_count.begin(), max_relation_tag_count.end()) << "\n";
    std::cout << "max relation members per block: " << *std::max_element(max_relation_member_count.begin(), max_relation_member_count.end()) << "\n";

    auto timestamp_to_str = [](const time_t in_time_t)->std::string
    {
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&in_time_t), "%F %T %Z");
        return ss.str();
    };

    std::cout << "max node timestamp: " << timestamp_to_str(*std::max_element(node_timestamp.begin(), node_timestamp.end())) << std::endl;
    std::cout << "max way timestamp: " << timestamp_to_str(*std::max_element(way_timestamp.begin(), way_timestamp.end())) << std::endl;
    std::cout << "max relation timestamp: " << timestamp_to_str(*std::max_element(relation_timestamp.begin(), relation_timestamp.end())) << std::endl;

    std::cout << "max file block index: " << *std::max_element(block_index.begin(), block_index.end()) << std::endl;
    return EXIT_SUCCESS;
}