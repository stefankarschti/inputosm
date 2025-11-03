#include <inputosm/inputosm.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "test_utils.h"

namespace
{
struct NodeData
{
    int64_t id = 0;
    int64_t raw_latitude = 0;
    int64_t raw_longitude = 0;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
    std::map<std::string, std::string> tags;
};

struct WayData
{
    int64_t id = 0;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
    std::vector<int64_t> refs;
    std::map<std::string, std::string> tags;
};

struct RelationMemberData
{
    uint8_t type = 0;
    int64_t ref = 0;
    std::string role;
};

struct RelationData
{
    int64_t id = 0;
    int32_t version = 0;
    int32_t timestamp = 0;
    int32_t changeset = 0;
    std::vector<RelationMemberData> members;
    std::map<std::string, std::string> tags;
};

template<typename T>
const T* find_by_id(const std::vector<T>& items, int64_t id)
{
    const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) { return item.id == id; });
    return it != items.end() ? &(*it) : nullptr;
}
}
int main()
{
    const auto data_path = std::filesystem::path(__FILE__).parent_path() / "data" / "sample.osm";
    if (!std::filesystem::exists(data_path))
    {
        std::cerr << "Missing test data file at " << data_path << '\n';
        return EXIT_FAILURE;
    }

    std::vector<NodeData> nodes;
    std::vector<WayData> ways;
    std::vector<RelationData> relations;

    const bool parse_ok = input_osm::input_file(
        data_path.string().c_str(),
        true,
        [&nodes](input_osm::span_t<input_osm::node_t> batch) {
            for (const auto& node : batch)
            {
                NodeData copy;
                copy.id = node.id;
                copy.raw_latitude = node.raw_latitude;
                copy.raw_longitude = node.raw_longitude;
                copy.version = node.version;
                copy.timestamp = node.timestamp;
                copy.changeset = node.changeset;
                for (const auto& tag : node.tags)
                {
                    copy.tags.emplace(tag.key ? tag.key : "", tag.value ? tag.value : "");
                }
                nodes.emplace_back(std::move(copy));
            }
            return true;
        },
        [&ways](input_osm::span_t<input_osm::way_t> batch) {
            for (const auto& way : batch)
            {
                WayData copy;
                copy.id = way.id;
                copy.version = way.version;
                copy.timestamp = way.timestamp;
                copy.changeset = way.changeset;
                copy.refs.assign(way.node_refs.begin(), way.node_refs.end());
                for (const auto& tag : way.tags)
                {
                    copy.tags.emplace(tag.key ? tag.key : "", tag.value ? tag.value : "");
                }
                ways.emplace_back(std::move(copy));
            }
            return true;
        },
        [&relations](input_osm::span_t<input_osm::relation_t> batch) {
            for (const auto& relation : batch)
            {
                RelationData copy;
                copy.id = relation.id;
                copy.version = relation.version;
                copy.timestamp = relation.timestamp;
                copy.changeset = relation.changeset;
                for (const auto& member : relation.members)
                {
                    RelationMemberData member_copy;
                    member_copy.type = member.type;
                    member_copy.ref = member.id;
                    member_copy.role = member.role ? member.role : "";
                    copy.members.emplace_back(std::move(member_copy));
                }
                for (const auto& tag : relation.tags)
                {
                    copy.tags.emplace(tag.key ? tag.key : "", tag.value ? tag.value : "");
                }
                relations.emplace_back(std::move(copy));
            }
            return true;
        });

    if (!parse_ok)
    {
        std::cerr << "input_file returned failure" << '\n';
        return EXIT_FAILURE;
    }

    if (nodes.size() != 2)
    {
        std::cerr << "Expected 2 nodes, got " << nodes.size() << '\n';
        return EXIT_FAILURE;
    }
    if (ways.size() != 1)
    {
        std::cerr << "Expected 1 way, got " << ways.size() << '\n';
        return EXIT_FAILURE;
    }
    if (relations.size() != 1)
    {
        std::cerr << "Expected 1 relation, got " << relations.size() << '\n';
        return EXIT_FAILURE;
    }

    const NodeData* node1 = find_by_id(nodes, 1);
    if (!node1)
    {
        std::cerr << "Node 1 not found" << '\n';
        return EXIT_FAILURE;
    }
    if (node1->raw_latitude != static_cast<int64_t>(52.5200 * 1e7))
    {
        std::cerr << "Unexpected raw latitude for node 1: " << node1->raw_latitude << '\n';
        return EXIT_FAILURE;
    }
    if (node1->raw_longitude != static_cast<int64_t>(13.4050 * 1e7))
    {
        std::cerr << "Unexpected raw longitude for node 1: " << node1->raw_longitude << '\n';
        return EXIT_FAILURE;
    }
    if (node1->version != 3)
    {
        std::cerr << "Unexpected version for node 1: " << node1->version << '\n';
        return EXIT_FAILURE;
    }
    if (node1->changeset != 111)
    {
        std::cerr << "Unexpected changeset for node 1: " << node1->changeset << '\n';
        return EXIT_FAILURE;
    }
    if (node1->timestamp != make_timestamp(2020, 1, 2, 3, 4, 5))
    {
        std::cerr << "Unexpected timestamp for node 1: " << node1->timestamp << '\n';
        return EXIT_FAILURE;
    }
    auto node1_name = node1->tags.find("name");
    if (node1_name == node1->tags.end() || node1_name->second != "Node One")
    {
        std::cerr << "Missing name tag on node 1" << '\n';
        return EXIT_FAILURE;
    }
    auto node1_amenity = node1->tags.find("amenity");
    if (node1_amenity == node1->tags.end() || node1_amenity->second != "cafe")
    {
        std::cerr << "Missing amenity tag on node 1" << '\n';
        return EXIT_FAILURE;
    }

    const NodeData* node2 = find_by_id(nodes, 2);
    if (!node2)
    {
        std::cerr << "Node 2 not found" << '\n';
        return EXIT_FAILURE;
    }
    if (node2->raw_latitude != static_cast<int64_t>(48.8566 * 1e7))
    {
        std::cerr << "Unexpected raw latitude for node 2: " << node2->raw_latitude << '\n';
        return EXIT_FAILURE;
    }
    if (node2->raw_longitude != static_cast<int64_t>(2.3522 * 1e7))
    {
        std::cerr << "Unexpected raw longitude for node 2: " << node2->raw_longitude << '\n';
        return EXIT_FAILURE;
    }
    if (node2->version != 2)
    {
        std::cerr << "Unexpected version for node 2" << '\n';
        return EXIT_FAILURE;
    }
    if (node2->changeset != 222)
    {
        std::cerr << "Unexpected changeset for node 2" << '\n';
        return EXIT_FAILURE;
    }
    if (node2->timestamp != make_timestamp(2020, 2, 3, 4, 5, 6))
    {
        std::cerr << "Unexpected timestamp for node 2" << '\n';
        return EXIT_FAILURE;
    }
    auto node2_name = node2->tags.find("name");
    if (node2_name == node2->tags.end() || node2_name->second != "Node Two")
    {
        std::cerr << "Missing name tag on node 2" << '\n';
        return EXIT_FAILURE;
    }

    const WayData* way10 = find_by_id(ways, 10);
    if (!way10)
    {
        std::cerr << "Way 10 not found" << '\n';
        return EXIT_FAILURE;
    }
    if (way10->refs != std::vector<int64_t>{1, 2})
    {
        std::cerr << "Unexpected node refs for way 10" << '\n';
        return EXIT_FAILURE;
    }
    if (way10->version != 4)
    {
        std::cerr << "Unexpected version for way 10" << '\n';
        return EXIT_FAILURE;
    }
    if (way10->changeset != 333)
    {
        std::cerr << "Unexpected changeset for way 10" << '\n';
        return EXIT_FAILURE;
    }
    if (way10->timestamp != make_timestamp(2020, 3, 4, 5, 6, 7))
    {
        std::cerr << "Unexpected timestamp for way 10" << '\n';
        return EXIT_FAILURE;
    }
    auto way10_highway = way10->tags.find("highway");
    if (way10_highway == way10->tags.end() || way10_highway->second != "residential")
    {
        std::cerr << "Missing highway tag on way 10" << '\n';
        return EXIT_FAILURE;
    }
    auto way10_name = way10->tags.find("name");
    if (way10_name == way10->tags.end() || way10_name->second != "A Street")
    {
        std::cerr << "Missing name tag on way 10" << '\n';
        return EXIT_FAILURE;
    }

    const RelationData* relation20 = find_by_id(relations, 20);
    if (!relation20)
    {
        std::cerr << "Relation 20 not found" << '\n';
        return EXIT_FAILURE;
    }
    if (relation20->members.size() != 2)
    {
        std::cerr << "Unexpected members count for relation 20" << '\n';
        return EXIT_FAILURE;
    }
    const auto& member_node = relation20->members[0];
    if (member_node.type != 0 || member_node.ref != 1 || member_node.role != "stop")
    {
        std::cerr << "Unexpected first member for relation 20" << '\n';
        return EXIT_FAILURE;
    }
    const auto& member_way = relation20->members[1];
    if (member_way.type != 1 || member_way.ref != 10 || member_way.role != "route")
    {
        std::cerr << "Unexpected second member for relation 20" << '\n';
        return EXIT_FAILURE;
    }
    if (relation20->version != 5)
    {
        std::cerr << "Unexpected version for relation 20" << '\n';
        return EXIT_FAILURE;
    }
    if (relation20->changeset != 444)
    {
        std::cerr << "Unexpected changeset for relation 20" << '\n';
        return EXIT_FAILURE;
    }
    if (relation20->timestamp != make_timestamp(2020, 4, 5, 6, 7, 8))
    {
        std::cerr << "Unexpected timestamp for relation 20" << '\n';
        return EXIT_FAILURE;
    }
    auto relation20_type = relation20->tags.find("type");
    if (relation20_type == relation20->tags.end() || relation20_type->second != "route")
    {
        std::cerr << "Missing type tag on relation 20" << '\n';
        return EXIT_FAILURE;
    }
    auto relation20_route = relation20->tags.find("route");
    if (relation20_route == relation20->tags.end() || relation20_route->second != "bus")
    {
        std::cerr << "Missing route tag on relation 20" << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
