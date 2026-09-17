#pragma once

#include <inputosm/inputosm.hpp>
#include <deque>
#include <limits>
#include <stdexcept>
#include <vector>

namespace pbf_test
{
// Collect borrowed batches for the existing complete-block assertions.
struct eager_block_t : input_osm::pbf_parameters_t
{
    size_t index = 0;
    uint64_t file_offset = 0;
    std::span<const std::string_view> string_table;
    std::span<const input_osm::node_t> nodes;
    std::span<const input_osm::way_t> ways;
    std::span<const input_osm::relation_t> relations;
};
using eager_handler_t = std::function<bool(const eager_block_t&)>;

inline bool collect(const input_osm::pbf_block_t& block, bool metadata, const eager_handler_t& handler)
{
    using namespace input_osm;
    eager_block_t result;
    result.index = block.index();
    result.file_offset = block.file_offset();
    if (!block.parameters(result)) return false;
    std::vector<std::string_view> strings;
    if (!block.decode_strings([&](auto value) {
            strings.push_back(value);
            return true;
        }))
        return false;
    result.string_table = strings;
    std::vector<node_t> nodes;
    std::vector<way_t> ways;
    std::vector<relation_t> relations;
    std::deque<std::vector<tag_t>> tags;
    std::deque<std::vector<int64_t>> refs;
    std::deque<std::vector<relation_member_t>> members;
    auto string = [&](uint32_t id) {
        if (id >= strings.size()) throw std::runtime_error("Invalid test string ID");
        return strings[id];
    };
    auto narrow = [](int64_t value) {
        if (value < INT32_MIN || value > INT32_MAX) throw std::runtime_error("Invalid legacy metadata value");
        return static_cast<int32_t>(value);
    };
    auto fields = [&](auto& target, const auto& source, size_t i) {
        target.id = source.ids[i];
        auto& list = tags.emplace_back();
        for (auto tag : source.tags[i]) list.push_back({string(tag.key), string(tag.value)});
        target.tags = list;
        if (metadata)
        {
            const auto& info = source.metadata[i];
            target.version = info.present & pbf_metadata_t::version_present ? info.version : 0;
            target.timestamp = narrow(info.raw_timestamp);
            target.changeset = narrow(info.changeset);
            if (info.present & pbf_metadata_t::user_present) (void)string(info.user_sid);
        }
    };
    const pbf_entity_options_t options{pbf_node_options_t{true, true, true, true, metadata},
                                       pbf_way_options_t{true, true, metadata, false},
                                       pbf_relation_options_t{true, true, true, true, metadata}};
    if (!block.decode_entities(options, [&](const pbf_group_batch_t& group) {
            for (size_t i = 0; i < group.nodes.count; ++i)
            {
                auto& node = nodes.emplace_back();
                fields(node, group.nodes, i);
                node.raw_latitude = group.nodes.raw_latitudes[i];
                node.raw_longitude = group.nodes.raw_longitudes[i];
            }
            for (size_t i = 0; i < group.ways.count; ++i)
            {
                auto& way = ways.emplace_back();
                fields(way, group.ways, i);
                const auto list = group.ways.node_refs[i];
                way.node_refs = refs.emplace_back(list.begin(), list.end());
            }
            for (size_t i = 0; i < group.relations.count; ++i)
            {
                auto& relation = relations.emplace_back();
                fields(relation, group.relations, i);
                auto& list = members.emplace_back();
                for (size_t j = group.relations.member_offsets[i]; j < group.relations.member_offsets[i + 1]; ++j)
                    list.push_back({static_cast<uint8_t>(group.relations.member_types[j]),
                                    group.relations.member_ids[j],
                                    string(group.relations.member_roles[j])});
                relation.members = list;
            }
            return true;
        }))
        return false;
    result.nodes = nodes;
    result.ways = ways;
    result.relations = relations;
    return handler(result);
}

class eager_reader_t : public input_osm::pbf_reader_t
{
public:
    bool read_blocks(bool metadata, const eager_handler_t& handler) noexcept
    {
        if (!handler) return false;
        return input_osm::pbf_reader_t::read_blocks(
            [&](const auto& block) { return collect(block, metadata, handler); });
    }
    bool read_block(size_t index, bool metadata, const eager_handler_t& handler) noexcept
    {
        if (!handler) return false;
        return input_osm::pbf_reader_t::read_block(
            index, [&](const auto& block) { return collect(block, metadata, handler); });
    }
};
} // namespace pbf_test
