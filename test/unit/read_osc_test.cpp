#include <inputosm/inputosm.h>

#include "test_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
std::map<std::string, std::string> collect_tags(input_osm::span_t<input_osm::tag_t> tags)
{
    std::map<std::string, std::string> result;
    for (const auto& tag : tags)
    {
        result.emplace(tag.key ? tag.key : "", tag.value ? tag.value : "");
    }
    return result;
}
} // namespace

int main()
{
    const auto data_path = std::filesystem::path(__FILE__).parent_path() / "data" / "sample.osc";
    if (!std::filesystem::exists(data_path))
    {
        std::cerr << "Missing test data file at " << data_path << '\n';
        return EXIT_FAILURE;
    }

    bool node_seen = false;
    bool way_seen = false;
    bool relation_seen = false;

    const bool parse_ok = input_osm::input_file(
        data_path.string().c_str(),
        true,
        [&](input_osm::span_t<input_osm::node_t> batch) {
            if (batch.size() != 1)
            {
                std::cerr << "OSC node batch expected 1 entry, got " << batch.size() << '\n';
                return false;
            }
            if (input_osm::osc_mode != input_osm::mode_t::create)
            {
                std::cerr << "OSC mode for node should be create" << '\n';
                return false;
            }
            const auto& node = batch[0];
            if (node.id != 100)
            {
                std::cerr << "Unexpected node id " << node.id << '\n';
                return false;
            }
            if (node.raw_latitude != 407128000 || node.raw_longitude != -740060000)
            {
                std::cerr << "Unexpected node coordinates" << '\n';
                return false;
            }
            if (node.version != 7 || node.changeset != 1234)
            {
                std::cerr << "Unexpected node metadata" << '\n';
                return false;
            }
            if (node.timestamp != make_timestamp(2021, 1, 2, 3, 4, 5))
            {
                std::cerr << "Unexpected node timestamp" << '\n';
                return false;
            }
            const auto tags = collect_tags(node.tags);
            const auto name_it = tags.find("name");
            if (name_it == tags.end() || name_it->second != "Create Node")
            {
                std::cerr << "Missing name tag on created node" << '\n';
                return false;
            }
            const auto note_it = tags.find("note");
            if (note_it == tags.end() || note_it->second != "created")
            {
                std::cerr << "Missing note tag on created node" << '\n';
                return false;
            }
            node_seen = true;
            return true;
        },
        [&](input_osm::span_t<input_osm::way_t> batch) {
            if (batch.size() != 1)
            {
                std::cerr << "OSC way batch expected 1 entry, got " << batch.size() << '\n';
                return false;
            }
            if (input_osm::osc_mode != input_osm::mode_t::modify)
            {
                std::cerr << "OSC mode for way should be modify" << '\n';
                return false;
            }
            const auto& way = batch[0];
            if (way.id != 200)
            {
                std::cerr << "Unexpected way id " << way.id << '\n';
                return false;
            }
            if (way.version != 8 || way.changeset != 2345)
            {
                std::cerr << "Unexpected way metadata" << '\n';
                return false;
            }
            if (way.timestamp != make_timestamp(2021, 2, 3, 4, 5, 6))
            {
                std::cerr << "Unexpected way timestamp" << '\n';
                return false;
            }
            const std::vector<int64_t> expected_refs{100, 101};
            if (!std::equal(way.node_refs.begin(), way.node_refs.end(), expected_refs.begin(), expected_refs.end()))
            {
                std::cerr << "Unexpected way node refs" << '\n';
                return false;
            }
            const auto tags = collect_tags(way.tags);
            const auto highway_it = tags.find("highway");
            if (highway_it == tags.end() || highway_it->second != "secondary")
            {
                std::cerr << "Missing highway tag on modified way" << '\n';
                return false;
            }
            const auto status_it = tags.find("status");
            if (status_it == tags.end() || status_it->second != "modified")
            {
                std::cerr << "Missing status tag on modified way" << '\n';
                return false;
            }
            way_seen = true;
            return true;
        },
        [&](input_osm::span_t<input_osm::relation_t> batch) {
            if (batch.size() != 1)
            {
                std::cerr << "OSC relation batch expected 1 entry, got " << batch.size() << '\n';
                return false;
            }
            if (input_osm::osc_mode != input_osm::mode_t::destroy)
            {
                std::cerr << "OSC mode for relation should be destroy" << '\n';
                return false;
            }
            const auto& relation = batch[0];
            if (relation.id != 300)
            {
                std::cerr << "Unexpected relation id " << relation.id << '\n';
                return false;
            }
            if (relation.version != 9 || relation.changeset != 3456)
            {
                std::cerr << "Unexpected relation metadata" << '\n';
                return false;
            }
            if (relation.timestamp != make_timestamp(2021, 3, 4, 5, 6, 7))
            {
                std::cerr << "Unexpected relation timestamp" << '\n';
                return false;
            }
            if (relation.members.size() != 2)
            {
                std::cerr << "Unexpected relation members size" << '\n';
                return false;
            }
            const auto& node_member = relation.members[0];
            if (node_member.type != 0 || node_member.id != 100 || !node_member.role ||
                std::string(node_member.role) != "stop")
            {
                std::cerr << "Unexpected first relation member" << '\n';
                return false;
            }
            const auto& way_member = relation.members[1];
            if (way_member.type != 1 || way_member.id != 200 || !way_member.role ||
                std::string(way_member.role) != "route")
            {
                std::cerr << "Unexpected second relation member" << '\n';
                return false;
            }
            const auto tags = collect_tags(relation.tags);
            const auto type_it = tags.find("type");
            if (type_it == tags.end() || type_it->second != "route")
            {
                std::cerr << "Missing type tag on deleted relation" << '\n';
                return false;
            }
            const auto route_it = tags.find("route");
            if (route_it == tags.end() || route_it->second != "tram")
            {
                std::cerr << "Missing route tag on deleted relation" << '\n';
                return false;
            }
            relation_seen = true;
            return true;
        });

    if (!parse_ok)
    {
        std::cerr << "input_file returned failure" << '\n';
        return EXIT_FAILURE;
    }

    if (!node_seen || !way_seen || !relation_seen)
    {
        std::cerr << "Did not observe all expected OSM change entities" << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
