#include <inputosm/inputosm.h>
#include "pbf_test_data.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>

namespace
{
using namespace input_osm;
using namespace pbf_test;
using namespace std::string_literals;

void check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << "Check failed at line " << line << ": " << expression << '\n';
        throw std::runtime_error(expression);
    }
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)

static_assert(std::is_same_v<decltype(tag_t::key), std::string_view>);
static_assert(std::is_same_v<decltype(relation_member_t::role), std::string_view>);
static_assert(std::is_same_v<decltype(node_t::tags), std::span<const tag_t>>);
static_assert(std::is_same_v<decltype(way_t::node_refs), std::span<const int64_t>>);
static_assert(std::is_same_v<decltype(relation_t::members), std::span<const relation_member_t>>);
static_assert(std::is_same_v<decltype(pbf_block_t::nodes), std::span<const node_t>>);
static_assert(std::is_same_v<decltype(pbf_block_t::ways), std::span<const way_t>>);
static_assert(std::is_same_v<decltype(pbf_block_t::relations), std::span<const relation_t>>);
static_assert(std::is_same_v<decltype(pbf_block_t::string_table), std::span<const std::string_view>>);

const tag_t* find_tag(std::span<const tag_t> tags, std::string_view key)
{
    const auto found = std::find_if(tags.begin(), tags.end(), [&](const auto& tag) { return tag.key == key; });
    CHECK(found != tags.end());
    return &*found;
}
int64_t expected_node_id(size_t index)
{
    return index < 17000 ? index + 1 : 5000000000LL + index;
}

struct totals_t
{
    size_t count;
    bool metadata;
    std::vector<bool> seen_nodes;
    std::array<bool, 12> seen_ways{};
    std::array<bool, 4> seen_relations{};
    std::unordered_map<const char*, size_t> strings;
    bool check_strings = false;

    totals_t(size_t node_count, bool read_metadata)
        : count(node_count),
          metadata(read_metadata),
          seen_nodes(count)
    {
    }
    template <class Entity>
    void info(const Entity& entity, size_t index)
    {
        CHECK(entity.version == (metadata ? static_cast<int32_t>(index % 7 + 1) : 0));
        CHECK(entity.timestamp == (metadata ? 1577923200 + static_cast<int32_t>(index) : 0));
        CHECK(entity.changeset == (metadata ? 10000 + static_cast<int32_t>(index % 17) : 0));
    }
    void view(std::string_view string)
    {
        if (!check_strings) return;
        CHECK(strings.contains(string.data()));
        CHECK(strings.at(string.data()) == string.size());
    }
    void tags(std::span<const tag_t> tags)
    {
        for (const auto& tag : tags)
        {
            view(tag.key);
            view(tag.value);
        }
    }
    void nodes(std::span<const node_t> nodes)
    {
        for (const auto& node : nodes)
        {
            const size_t i = static_cast<size_t>(node.id > 5000000000LL ? node.id - 5000000000LL : node.id - 1);
            CHECK(i < count && !seen_nodes[i]);
            seen_nodes[i] = true;
            info(node, i);
            CHECK(node.raw_latitude == -450000000LL + static_cast<int64_t>(i % 9000) * 1000);
            CHECK(node.raw_longitude == 1200000000LL - static_cast<int64_t>(i % 10000) * 1000);
            CHECK(node.tags.size() == (i % 3 == 0 ? 3u : 0u) + (i == 0 ? 270u : 0u) + (i == count - 1 ? 1u : 0u));
            if (i % 3 == 0)
            {
                CHECK(find_tag(node.tags, "name")->value == "node " + std::to_string(i) + " – café");
                CHECK(find_tag(node.tags, "source")->value == "fixture");
                CHECK(find_tag(node.tags, "empty")->value.empty());
            }
            if (i == 0)
                for (size_t tag = 0; tag < 270; ++tag)
                    CHECK(find_tag(node.tags, "key" + std::to_string(tag))->value == "value" + std::to_string(tag));
            if (i == count - 1) CHECK(find_tag(node.tags, "long")->value == std::string(255, 'x'));
            tags(node.tags);
        }
    }
    void ways(std::span<const way_t> ways)
    {
        for (const auto& way : ways)
        {
            const size_t i = static_cast<size_t>(way.id - 10000000000LL);
            CHECK(i < seen_ways.size() && !seen_ways[i]);
            seen_ways[i] = true;
            info(way, i + 100);
            CHECK(way.tags.size() == 3);
            CHECK(find_tag(way.tags, "route")->value == (i % 4 == 0 ? "ferry" : "road"));
            CHECK(find_tag(way.tags, "name")->value == "way " + std::to_string(i));
            CHECK(find_tag(way.tags, "empty")->value.empty());
            CHECK(way.node_refs.size() == (i == 0 ? count : 4));
            if (i == 0)
            {
                for (size_t ref = 0; ref < count; ++ref) CHECK(way.node_refs[ref] == expected_node_id(ref));
            }
            else
            {
                CHECK(way.node_refs[0] == expected_node_id(count - 1));
                CHECK(way.node_refs[1] == 1 && way.node_refs[2] == 2 && way.node_refs[3] == 1);
            }
            tags(way.tags);
        }
    }
    void relations(std::span<const relation_t> relations)
    {
        for (const auto& relation : relations)
        {
            const size_t i = static_cast<size_t>(relation.id - 20000000000LL);
            CHECK(i < seen_relations.size() && !seen_relations[i]);
            seen_relations[i] = true;
            info(relation, i + 200);
            CHECK(relation.tags.size() == 2);
            CHECK(find_tag(relation.tags, "name")->value == "relation " + std::to_string(i));
            CHECK(find_tag(relation.tags, "type")->value == "route");
            CHECK(relation.members.size() == 3);
            CHECK(relation.members[0].type == 0 && relation.members[0].id == 1 && relation.members[0].role == "stop");
            CHECK(relation.members[1].type == 1 && relation.members[1].id == 10000000000LL + static_cast<int64_t>(i));
            CHECK(relation.members[1].role.empty());
            CHECK(relation.members[2].type == 2 &&
                  relation.members[2].id == 20000000000LL + static_cast<int64_t>((i + 1) % 4));
            CHECK(relation.members[2].role == "café");
            tags(relation.tags);
            for (const auto& member : relation.members) view(member.role);
        }
    }
    void finish()
    {
        CHECK(std::all_of(seen_nodes.begin(), seen_nodes.end(), [](bool value) { return value; }));
        CHECK(std::all_of(seen_ways.begin(), seen_ways.end(), [](bool value) { return value; }));
        CHECK(std::all_of(seen_relations.begin(), seen_relations.end(), [](bool value) { return value; }));
    }
};

void fixture_test(std::string_view name, size_t count, bool metadata, size_t threads)
{
    set_thread_count(threads);
    const auto path = (std::filesystem::path(__FILE__).parent_path() / "data" / name).string();
    totals_t totals(count, metadata);
    std::mutex mutex;
    size_t calls = 0, last_index = 0;
    uint64_t last_offset = 0;
    const auto caller = std::this_thread::get_id();
    CHECK(input_pbf_blocks(path.c_str(), metadata, [&](const pbf_block_t& block) {
        std::lock_guard lock(mutex);
        CHECK(thread_index < thread_count() && block_index == block.index);
        CHECK(file_type == file_type_t::pbf && osc_mode == input_osm::mode_t::bulk);
        CHECK(block.index > 0 && block.file_offset > 0);
        if (threads == 1)
        {
            CHECK(std::this_thread::get_id() == caller);
            CHECK(block.index > last_index && block.file_offset > last_offset);
        }
        last_index = block.index;
        last_offset = block.file_offset;
        CHECK(block.granularity == 100 && block.date_granularity == 1000 && block.lat_offset == 0 &&
              block.lon_offset == 0);
        CHECK(!block.string_table.empty() && block.string_table[0].empty());
        totals.strings.clear();
        for (auto string : block.string_table) totals.strings.emplace(string.data(), string.size());
        totals.check_strings = true;
        totals.nodes(block.nodes);
        totals.ways(block.ways);
        totals.relations(block.relations);
        ++calls;
        return true;
    }));
    CHECK(calls >= 3);
    totals.finish();
    totals_t batches(count, metadata);
    CHECK(input_file(
        path.c_str(),
        metadata,
        [&](std::span<const node_t> nodes) {
            std::lock_guard lock(mutex);
            batches.nodes(nodes);
            return true;
        },
        [&](std::span<const way_t> ways) {
            std::lock_guard lock(mutex);
            batches.ways(ways);
            return true;
        },
        [&](std::span<const relation_t> relations) {
            std::lock_guard lock(mutex);
            batches.relations(relations);
            return true;
        }));
    batches.finish();
    CHECK(input_file(path.c_str(), metadata, {}, {}, {}));
}

bool keep_block(const pbf_block_t&)
{
    return true;
}

void layout_test(files_t& files)
{
    set_thread_count(1);
    const std::string embedded("A\0B", 3);
    const auto first_node = node(-8) + integer(2, 1) + packed(3, {2}) + message(4, integer(1, 4));
    const auto dense = packed(9, {sint(400), sint(-100)}) + packed(1, {sint(20)}) + integer(1, sint(-2)) +
                       packed(8, {sint(-200), sint(100)}) + packed(10, {1, 3, 0, 0});
    const auto way = integer(1, 99) + packed(8, {sint(20), sint(-2)}) + integer(2, 1) + integer(3, 2);
    const auto relation = integer(1, 101) + packed(8, {3}) + packed(9, {sint(-8)}) + packed(10, {0});
    const auto groups = message(2, message(1, first_node)) + message(2, message(2, dense)) +
                        message(2, message(3, way)) + message(2, message(4, relation)) +
                        message(2, message(1, node(123)));
    const auto strings = table({"", "name", "value", embedded, "unused", "value", ""});
    const auto payload = groups + strings + integer(17, 10) + integer(18, 100) +
                         integer(19, std::bit_cast<uint64_t>(int64_t{-5})) + integer(20, 7) + integer(100, 88);
    for (bool zip : {false, true})
    {
        const auto initial = header();
        const auto skip = file_block("Unknown", "opaque");
        const auto data = zip ? zlib_block(payload) : raw_block("OSMData", payload);
        const auto file = initial + skip + data + raw_block("OSMData", table());
        const auto path = files.write(file, ".binary");
        size_t calls = 0;
        CHECK(input_pbf_blocks(path.c_str(), true, [&](const pbf_block_t& block) {
            if (++calls == 1)
            {
                CHECK(block.index == 2 && block.file_offset == initial.size() + skip.size());
                CHECK(block.nodes.size() == 4 && block.ways.size() == 1 && block.relations.size() == 1);
                CHECK(block.nodes[0].id == -8 && block.nodes[0].version == 4);
                CHECK(block.nodes[1].id == 20 && block.nodes[2].id == 18 && block.nodes[3].id == 123);
                CHECK(block.nodes[1].raw_latitude == -200 && block.nodes[2].raw_longitude == 300);
                CHECK(block.nodes[1].tags[0].value == embedded && block.nodes[2].tags.empty());
                CHECK(block.relations[0].members[0].id == -8 && block.relations[0].members[0].role == embedded);
                CHECK(block.ways[0].node_refs[0] == 20 && block.ways[0].node_refs[1] == 18);
                CHECK(block.string_table.size() == 7 && block.string_table[4] == "unused" &&
                      block.string_table[6].empty());
                CHECK(block.string_table[2] == block.string_table[5]);
                CHECK(block.string_table[2].data() != block.string_table[5].data());
                CHECK(block.string_table[2].data() - block.string_table[1].data() == 6);
                CHECK(block.nodes[0].tags[0].key.data() == block.string_table[1].data());
                CHECK(block.relations[0].members[0].role.data() == block.string_table[3].data());
                CHECK(block.granularity == 10 && block.date_granularity == 100 && block.lat_offset == -5 &&
                      block.lon_offset == 7);
            }
            else
            {
                CHECK(block.index == 3 && block.file_offset == initial.size() + skip.size() + data.size());
                CHECK(block.nodes.empty() && block.ways.empty() && block.relations.empty());
                CHECK(block.granularity == 100 && block.date_granularity == 1000 && block.lat_offset == 0 &&
                      block.lon_offset == 0);
            }
            return true;
        }));
        CHECK(calls == 2);
        const auto batches_path = files.write(file);
        std::vector<size_t> node_batches, way_batches, relation_batches;
        CHECK(input_file(
            batches_path.c_str(),
            false,
            [&](std::span<const node_t> nodes) {
                node_batches.push_back(nodes.size());
                return true;
            },
            [&](std::span<const way_t> ways) {
                way_batches.push_back(ways.size());
                return true;
            },
            [&](std::span<const relation_t> relations) {
                relation_batches.push_back(relations.size());
                return true;
            }));
        CHECK(node_batches == std::vector<size_t>({1, 2, 1}));
        CHECK(way_batches == std::vector<size_t>({0, 0, 1, 0, 0}));
        CHECK(relation_batches == std::vector<size_t>({0, 0, 0, 1, 0}));
    }
    const auto unknown_group = varint((99u << 3) | 3) + integer(1, 7) + varint((99u << 3) | 4);
    const auto extended = files.write(header() + raw_block("OSMData", primitive(message(1, node())) + unknown_group));
    CHECK(input_pbf_blocks(extended.c_str(), false, keep_block));
    const auto split_bbox = raw_block("OSMHeader",
                                      message(4, "OsmSchema-V0.6") + message(1, integer(1, 0) + integer(2, 0)) +
                                          message(1, integer(3, 0) + integer(4, 0)));
    const auto bbox_path = files.write(split_bbox);
    CHECK(input_pbf_blocks(bbox_path.c_str(), false, keep_block));
    const auto dense_first = packed(1, {sint(10)}) + packed(8, {sint(100)}) + packed(9, {sint(200)}) +
                             message(5,
                                     integer(1, 2) + integer(2, sint(50)) + integer(3, sint(70)) + integer(5, sint(1)));
    const auto dense_second = packed(1, {sint(-1)}) + packed(8, {sint(-20)}) + packed(9, {sint(30)}) +
                              message(
                                  5, integer(1, 3) + integer(2, sint(-2)) + integer(3, sint(-3)) + integer(5, sint(0)));
    const auto split_dense = files.write(header() +
                                         zlib_block(table({"", "metadata user", "unused"}) +
                                                    message(2, message(2, dense_first) + message(2, dense_second))));
    for (bool metadata : {false, true})
        CHECK(input_pbf_blocks(split_dense.c_str(), metadata, [&](const pbf_block_t& block) {
            CHECK(block.string_table.size() == 3 && block.string_table[1] == "metadata user" &&
                  block.string_table[2] == "unused");
            CHECK(block.nodes.size() == 2 && block.nodes[0].id == 10 && block.nodes[1].id == 9);
            CHECK(block.nodes[1].raw_latitude == 80 && block.nodes[1].raw_longitude == 230);
            CHECK(block.nodes[0].version == (metadata ? 2 : 0));
            CHECK(block.nodes[1].version == (metadata ? 3 : 0));
            CHECK(block.nodes[0].timestamp == (metadata ? 50 : 0));
            CHECK(block.nodes[1].timestamp == (metadata ? 48 : 0));
            CHECK(block.nodes[1].changeset == (metadata ? 67 : 0));
            return true;
        }));
    size_t calls = 0;
    const auto only_header = files.write(header());
    CHECK(input_pbf_blocks(only_header.c_str(), false, [&](const auto&) {
        ++calls;
        return true;
    }));
    CHECK(calls == 0);
}

void column_test(files_t& files)
{
    set_thread_count(1);
    auto split = [](uint32_t number, std::initializer_list<uint64_t> values) {
        std::string result = packed(number, {});
        bool scalar = true;
        for (auto value : values)
        {
            result += scalar ? integer(number, value) : packed(number, {value});
            scalar = !scalar;
        }
        return result;
    };
    const auto keys = split(2, {1, 2, 1}), values = split(3, {2, 0, 3});
    const std::array<std::string, 3> members = {
        split(8, {0, 3, 2}), split(9, {sint(1), sint(4999999999LL), sint(-4999999997LL)}), split(10, {2, 0, 1})};
    std::array<size_t, 3> order{0, 1, 2};
    std::array<std::string, 10> dense_columns = {split(1, {sint(10), sint(-1), sint(3)}),
                                                 split(8, {sint(100), sint(-20), sint(5)}),
                                                 split(9, {sint(200), sint(30), sint(-20)}),
                                                 split(10, {1, 2, 0, 0, 2, 0, 1, 3, 0}),
                                                 message(5, split(1, {2, 3, 4})),
                                                 message(5, split(2, {sint(50), sint(-2), sint(1)})),
                                                 message(5, split(3, {sint(70), sint(-3), sint(4)})),
                                                 message(5, split(4, {sint(100), sint(-10), sint(1)})),
                                                 message(5, split(5, {sint(3), sint(-1), sint(1)})),
                                                 message(5, split(6, {1, 0, 1}))};
    size_t permutation = 0;
    do
    {
        for (bool values_first : {false, true})
        {
            const auto tags = values_first ? values + keys : keys + values;
            const auto way = split(8, {sint(5000000000LL), sint(-2), sint(1)}) + tags + integer(1, 99);
            const auto relation = members[order[0]] + tags + members[order[1]] + integer(1, 101) + members[order[2]];
            std::string dense_group;
            for (const auto& column : dense_columns) dense_group += message(2, column);
            std::rotate(dense_columns.begin(), dense_columns.begin() + 1, dense_columns.end());
            const auto empty_node = packed(1, {sint(1)}) + packed(8, {0}) + packed(9, {0});
            const auto payload = table({"", "key", "value", "user"}) + message(2, message(1, node(99) + tags)) +
                                 message(2, dense_group) + message(2, message(3, way)) +
                                 message(2, message(4, relation)) + message(2, message(2, empty_node));
            const auto path = files.write(header() +
                                          (permutation++ % 2 ? zlib_block(payload) : raw_block("OSMData", payload)));
            for (bool metadata : {false, true})
            {
                auto check_tags = [](std::span<const tag_t> tags) {
                    CHECK(tags.size() == 3);
                    CHECK(tags[0].key == "key" && tags[0].value == "value");
                    CHECK(tags[1].key == "value" && tags[1].value.empty());
                    CHECK(tags[2].key == "key" && tags[2].value == "user");
                };
                size_t nodes_seen = 0, ways_seen = 0, relations_seen = 0;
                auto nodes = [&](std::span<const node_t> nodes) {
                    for (const auto& node : nodes)
                    {
                        const std::array<int64_t, 5> ids{99, 10, 9, 12, 1};
                        CHECK(nodes_seen < ids.size() && node.id == ids[nodes_seen]);
                        if (nodes_seen == 0) check_tags(node.tags);
                        if (nodes_seen >= 1 && nodes_seen <= 3)
                        {
                            const auto i = nodes_seen - 1;
                            CHECK(node.raw_latitude == (std::array<int64_t, 3>{100, 80, 85})[i]);
                            CHECK(node.raw_longitude == (std::array<int64_t, 3>{200, 230, 210})[i]);
                            CHECK(node.version == (metadata ? static_cast<int32_t>(i + 2) : 0));
                            CHECK(node.timestamp == (metadata ? (std::array<int32_t, 3>{50, 48, 49})[i] : 0));
                            CHECK(node.changeset == (metadata ? (std::array<int32_t, 3>{70, 67, 71})[i] : 0));
                            CHECK(node.tags.size() == (std::array<size_t, 3>{1, 0, 2})[i]);
                            if (i == 0) CHECK(node.tags[0].key == "key" && node.tags[0].value == "value");
                            if (i == 2)
                            {
                                CHECK(node.tags[0].key == "value" && node.tags[0].value.empty());
                                CHECK(node.tags[1].key == "key" && node.tags[1].value == "user");
                            }
                        }
                        else
                        {
                            CHECK(node.version == 0 && node.timestamp == 0 && node.changeset == 0);
                            if (nodes_seen == 4) CHECK(node.tags.empty());
                        }
                        ++nodes_seen;
                    }
                    return true;
                };
                auto ways = [&](std::span<const way_t> ways) {
                    for (const auto& way : ways)
                    {
                        CHECK(way.id == 99 && way.node_refs.size() == 3);
                        CHECK(way.node_refs[0] == 5000000000LL && way.node_refs[1] == 4999999998LL &&
                              way.node_refs[2] == 4999999999LL);
                        check_tags(way.tags);
                        ++ways_seen;
                    }
                    return true;
                };
                auto relations = [&](std::span<const relation_t> relations) {
                    for (const auto& relation : relations)
                    {
                        CHECK(relation.id == 101 && relation.members.size() == 3);
                        for (size_t i = 0; i < 3; ++i)
                        {
                            CHECK(relation.members[i].id == (std::array<int64_t, 3>{1, 5000000000LL, 3})[i]);
                            CHECK(relation.members[i].type == (std::array<uint8_t, 3>{2, 0, 1})[i]);
                            CHECK(relation.members[i].role ==
                                  (std::array<std::string_view, 3>{"", "user", "value"})[i]);
                        }
                        check_tags(relation.tags);
                        ++relations_seen;
                    }
                    return true;
                };
                CHECK(input_pbf_blocks(path.c_str(), metadata, [&](const pbf_block_t& block) {
                    return nodes(block.nodes) && ways(block.ways) && relations(block.relations);
                }));
                CHECK(nodes_seen == 5 && ways_seen == 1 && relations_seen == 1);
                nodes_seen = ways_seen = relations_seen = 0;
                CHECK(input_file(path.c_str(), metadata, nodes, ways, relations));
                CHECK(nodes_seen == 5 && ways_seen == 1 && relations_seen == 1);
            }
        }
    } while (std::next_permutation(order.begin(), order.end()));
}

void dense_limits_test(files_t& files)
{
    set_thread_count(1);
    const auto maximum = std::numeric_limits<int64_t>::max();
    const auto minimum = std::numeric_limits<int64_t>::min();
    const auto dense = packed(9, {0, sint(maximum), sint(-maximum)}) +
                       packed(1, {sint(maximum), sint(-maximum), sint(minimum)}) +
                       packed(8, {sint(minimum), sint(maximum), sint(1)});
    const auto empty = packed(1, {}) + packed(8, {}) + packed(9, {});
    const auto zero = packed(1, {0}) + packed(8, {0}) + packed(9, {0});
    const auto path = files.write(header() +
                                  raw_block("OSMData",
                                            table({""}) + message(2, message(2, empty)) +
                                                message(2, message(2, dense)) + message(2, message(2, zero))));
    for (bool metadata : {false, true})
    {
        size_t count = 0;
        auto nodes = [&](std::span<const node_t> nodes) {
            const std::array<int64_t, 4> ids{maximum, 0, minimum, 0};
            const std::array<int64_t, 4> latitudes{minimum, -1, 0, 0};
            const std::array<int64_t, 4> longitudes{0, maximum, 0, 0};
            for (const auto& node : nodes)
            {
                CHECK(count < ids.size());
                CHECK(node.id == ids[count]);
                CHECK(node.raw_latitude == latitudes[count]);
                CHECK(node.raw_longitude == longitudes[count]);
                CHECK(node.tags.empty());
                CHECK(node.version == 0 && node.timestamp == 0 && node.changeset == 0);
                ++count;
            }
            return true;
        };
        CHECK(input_pbf_blocks(path.c_str(), metadata, [&](const pbf_block_t& block) { return nodes(block.nodes); }));
        CHECK(count == 4);
        count = 0;
        CHECK(input_file(path.c_str(), metadata, nodes, {}, {}));
        CHECK(count == 4);
    }
}

void growth_test(files_t& files)
{
    const std::string long_string(8192, 'x');
    std::string payload = table({"", "key", long_string});
    for (size_t group = 0; group < 12; ++group)
    {
        std::string entities;
        for (size_t index = 0; index < 300; ++index)
            entities += message(1, node(static_cast<int64_t>(300 * group + index)) + packed(2, {1}) + packed(3, {2}));
        payload += message(2, entities);
        std::string refs, roles, types;
        for (size_t i = 0; i < 500; ++i)
        {
            refs += varint(sint(1));
            roles += varint(2);
            types += varint(i % 3);
        }
        const auto tags = packed(2, {1}) + packed(3, {2});
        payload += message(2, message(3, integer(1, group) + message(8, refs) + tags));
        payload += message(
            2, message(4, integer(1, group) + message(8, roles) + message(9, refs) + message(10, types) + tags));
    }
    const auto path = files.write(header() + zlib_block(payload));
    CHECK(input_pbf_blocks(path.c_str(), false, [&](const pbf_block_t& block) {
        CHECK(block.nodes.size() == 3600 && block.ways.size() == 12 && block.relations.size() == 12);
        for (size_t group = 0; group < 12; ++group)
        {
            CHECK(block.ways[group].node_refs.size() == 500 && block.relations[group].members.size() == 500);
            CHECK(block.ways[group].tags[0].value == long_string);
            CHECK(block.relations[group].tags[0].value == long_string);
            for (size_t i = 0; i < 500; ++i)
            {
                CHECK(block.ways[group].node_refs[i] == static_cast<int64_t>(i + 1));
                CHECK(block.relations[group].members[i].id == static_cast<int64_t>(i + 1));
                CHECK(block.relations[group].members[i].type == i % 3);
                CHECK(block.relations[group].members[i].role == long_string);
            }
        }
        for (size_t i = 0; i < block.nodes.size(); ++i)
        {
            CHECK(block.nodes[i].id == static_cast<int64_t>(i));
            CHECK(block.nodes[i].tags[0].value == long_string);
            CHECK(block.nodes[i].tags[0].value.data() == block.string_table[2].data());
        }
        return true;
    }));
    std::string xml = "<osm version=\"0.6\"><relation id=\"1\">";
    for (size_t i = 0; i < 500; ++i)
        xml += "<tag k=\"k" + std::to_string(i) + "\" v=\"" + std::string(i % 2 ? 200 : 3, 'x') + "\"/>";
    xml += "<member type=\"node\" ref=\"1\"/><member type=\"way\" ref=\"2\" role=\"\"/></relation></osm>";
    const auto xml_path = files.write(xml, ".osm");
    bool called = false;
    CHECK(input_file(xml_path.c_str(), false, {}, {}, [&](std::span<const relation_t> relations) {
        CHECK(relations.size() == 1 && relations[0].tags.size() == 500);
        for (size_t i = 0; i < 500; ++i)
        {
            CHECK(relations[0].tags[i].key == "k" + std::to_string(i));
            CHECK(relations[0].tags[i].value == std::string(i % 2 ? 200 : 3, 'x'));
        }
        CHECK(relations[0].members.size() == 2);
        CHECK(relations[0].members[0].role.empty() && relations[0].members[1].role.empty());
        called = true;
        return true;
    }));
    CHECK(called);
}

void error_test(files_t& files)
{
    set_thread_count(1);
    const auto good_payload = primitive(message(1, node()));
    const auto good = header() + raw_block("OSMData", good_payload);
    const auto good_path = files.write(good);
    auto reject = [&](const std::string& bytes) {
        const auto path = files.write(bytes);
        CHECK(!input_pbf_blocks(path.c_str(), true, keep_block));
        CHECK(input_pbf_blocks(good_path.c_str(), true, keep_block));
    };
    CHECK(!input_pbf_blocks(nullptr, false, keep_block));
    CHECK(!input_pbf_blocks("/inputosm-file-that-does-not-exist", false, keep_block));
    CHECK(!input_pbf_blocks(good_path.c_str(), false, {}));
    reject("<osm version=\"0.6\"/>");
    for (size_t i = 0; i < good.size(); ++i)
        if (i != header().size()) reject(good.substr(0, i));
    reject(good + "\0"s);
    reject(raw_block("OSMData", good_payload));
    reject(header("Unsupported"));
    reject(header("HistoricalInformation"));
    reject(header() + header());
    reject(raw_block("OSMHeader", message(1, integer(1, 0))));
    reject(header() + raw_block("OSMData", good_payload + varint((99u << 3) | 4)));
    reject(header() + raw_block("OSMData", good_payload + varint((99u << 3) | 3) + varint((98u << 3) | 4)));
    std::string nested;
    for (size_t i = 0; i < 65; ++i) nested += varint((99u << 3) | 3);
    for (size_t i = 0; i < 65; ++i) nested += varint((99u << 3) | 4);
    reject(header() + raw_block("OSMData", good_payload + nested));
    reject(prefix(65536));
    reject(prefix(0));
    reject(header() + prefix(4) + integer(3, 0));
    reject(header() + file_block("OSMData", message(7, "unsupported")));
    reject(header() + file_block("OSMData", integer(2, 33554432) + message(3, "invalid")));
    reject(header() + file_block("OSMData", integer(2, 5) + message(3, "invalid")));
    reject(header() +
           file_block("OSMData", integer(2, good_payload.size() + 1) + message(3, compressed(good_payload))));
    reject(header() +
           file_block("OSMData", integer(2, good_payload.size()) + message(3, compressed(good_payload) + "x")));
    reject(header() + file_block("OSMData", message(1, good_payload) + message(3, "x")));
    reject(header() + file_block("OSMData", integer(2, 3) + message(1, good_payload)));
    reject(header() +
           file_block("OSMData", integer(2, std::numeric_limits<uint64_t>::max()) + message(1, good_payload)));
    reject(header() + file_block("OSMData", ""));
    const std::vector<std::string> invalid = {
        "",
        message(2, message(1, node())),
        message(1, ""),
        table({"wrong"}),
        table() + "\0"s,
        table() + std::string(11, static_cast<char>(0x80)),
        table() + varint((uint64_t{1} << 32) | 2) + varint(std::numeric_limits<uint64_t>::max()),
        table() + message(2, message(1, integer(1, 2))),
        primitive(message(1, node() + integer(2, 1))),
        primitive(message(1, node() + integer(2, 999) + integer(3, 2))),
        primitive(message(1, node() + integer(2, uint64_t{1} << 32) + integer(3, 2))),
        primitive(message(1, node()) + message(3, integer(1, 5))),
        primitive(message(2, packed(1, {2}) + packed(8, {4}))),
        primitive(message(2, packed(1, {2}) + packed(8, {4}) + packed(9, {6}) + packed(10, {1}))),
        primitive(message(2, packed(1, {2}) + packed(8, {4}) + packed(9, {6}) + packed(10, {1, 2}))),
        primitive(message(2, packed(1, {2}) + packed(8, {4}) + packed(9, {6}) + packed(10, {0, 0}))),
        primitive(message(2, packed(1, {2}) + packed(8, {4}) + packed(9, {6}) + message(5, packed(1, {1, 2})))),
        primitive(message(3, integer(1, 5) + packed(8, {sint(std::numeric_limits<int64_t>::max()), sint(1)}))),
        primitive(message(4, integer(1, 9) + packed(8, {1}) + packed(9, {2}))),
        primitive(message(4, integer(1, 9) + packed(8, {1}) + packed(9, {2}) + packed(10, {3}))),
        primitive(message(5, integer(1, 5))),
        primitive() + integer(17, 0),
        primitive() + integer(18, 0),
    };
    for (const auto& payload : invalid) reject(header() + raw_block("OSMData", payload));
    const auto overflow = files.write(
        header() + raw_block("OSMData", primitive(message(1, node() + message(4, integer(2, uint64_t{1} << 40))))));
    CHECK(!input_pbf_blocks(overflow.c_str(), true, keep_block));
    CHECK(input_pbf_blocks(overflow.c_str(), false, keep_block));
    size_t callbacks = 0;
    const auto partial = files.write(
        header() + raw_block("OSMData", table() + message(2, message(1, node())) + message(2, message(3, ""))));
    CHECK(!input_pbf_blocks(partial.c_str(), false, [&](const auto&) {
        ++callbacks;
        return true;
    }));
    CHECK(callbacks == 0);
}

void column_error_test(files_t& files)
{
    set_thread_count(1);
    auto reject = [&](std::string_view group, bool metadata = true) {
        const auto path = files.write(header() + raw_block("OSMData", primitive(group)));
        size_t calls = 0;
        CHECK(!input_pbf_blocks(path.c_str(), metadata, [&](const auto&) {
            ++calls;
            return true;
        }));
        CHECK(calls == 0);
        CHECK(!input_file(path.c_str(), metadata, {}, {}, {}));
    };
    for (size_t count : {0, 1, 3})
    {
        std::string extra;
        for (size_t i = 0; i < count; ++i) extra += varint(1);
        for (uint32_t field : {2, 3})
        {
            const auto tags = message(field, extra) + packed(field == 2 ? 3 : 2, {1, 2});
            reject(message(1, node() + tags));
            reject(message(3, tags + integer(1, 1)));
            reject(message(4, tags + integer(1, 1)));
        }
        for (uint32_t field : {8, 9, 10})
        {
            std::string relation = integer(1, 1) + message(field, extra);
            for (uint32_t other : {8, 9, 10})
                if (other != field) relation += packed(other, {1, 2});
            reject(message(4, relation));
        }
        for (uint32_t field : {1, 8, 9})
        {
            std::string dense = message(field, extra);
            for (uint32_t other : {1, 8, 9})
                if (other != field) dense += packed(other, {2, 2});
            reject(message(2, dense));
        }
        if (count == 0) continue;
        for (uint32_t field = 1; field <= 6; ++field)
            reject(message(
                2, message(5, message(field, extra)) + packed(1, {2, 2}) + packed(8, {2, 2}) + packed(9, {2, 2})));
    }
    const auto maximum = std::numeric_limits<int64_t>::max();
    const auto minimum = std::numeric_limits<int64_t>::min();
    for (const auto& deltas :
         {packed(8, {sint(maximum)}) + integer(8, sint(1)), packed(8, {sint(minimum)}) + integer(8, sint(-1))})
        reject(message(3, integer(1, 1) + deltas));
    for (uint32_t column : {1, 8, 9})
    {
        for (const auto& deltas : {packed(column, {sint(maximum)}) + integer(column, sint(1)),
                                   packed(column, {sint(minimum)}) + integer(column, sint(-1)),
                                   packed(column, {sint(maximum), sint(1)}),
                                   packed(column, {sint(minimum), sint(-1)})})
        {
            std::string dense = deltas;
            for (uint32_t other : {1, 8, 9})
                if (other != column) dense += packed(other, {0, 0});
            for (bool metadata : {false, true}) reject(message(2, dense), metadata);
        }
        for (const auto& invalid :
             {std::string(1, static_cast<char>(0x80)), std::string(9, static_cast<char>(0xff)) + "\2"})
        {
            std::string dense = message(column, invalid);
            for (uint32_t other : {1, 8, 9})
                if (other != column) dense += packed(other, {0});
            for (bool metadata : {false, true}) reject(message(2, dense), metadata);
        }
    }
    reject(message(
        4, integer(1, 1) + packed(10, {0, 0}) + packed(8, {0, 0}) + packed(9, {sint(minimum)}) + integer(9, sint(-1))));
    const auto dense_node = packed(1, {2}) + packed(8, {2}) + packed(9, {2});
    for (const auto& info : {packed(1, {uint64_t{1} << 31}),
                             packed(2, {sint(int64_t{1} << 31)}),
                             packed(3, {sint(int64_t{1} << 31)}),
                             packed(4, {sint(int64_t{1} << 31)}),
                             packed(5, {sint(-1)}),
                             packed(5, {sint(3)})})
    {
        const auto group = message(2, dense_node + message(5, info));
        reject(group);
        const auto path = files.write(header() + raw_block("OSMData", primitive(group)));
        CHECK(input_pbf_blocks(path.c_str(), false, keep_block));
    }
    for (size_t length = 1; length <= 10; ++length)
    {
        const auto invalid = message(8, std::string(length, static_cast<char>(0x80)));
        reject(message(3, integer(1, 1) + invalid));
    }
    reject(message(3, integer(1, 1) + message(8, std::string(9, static_cast<char>(0xff)) + "\2")));
    for (int bits = 0; bits <= 63; bits += 7)
    {
        const int64_t id = bits == 63 ? minimum : int64_t{1} << bits;
        const auto path = files.write(header() + raw_block("OSMData", primitive(message(1, node(id)))));
        size_t calls = 0;
        CHECK(input_pbf_blocks(path.c_str(), true, [&](const pbf_block_t& block) {
            CHECK(block.nodes.size() == 1 && block.nodes[0].id == id);
            ++calls;
            return true;
        }));
        CHECK(calls == 1);
    }
}

void reader_boundary_test(files_t& files)
{
    set_thread_count(1);
    auto check_payload = [&](const std::string& payload, bool expected) {
        const auto path = files.write(header() + raw_block("OSMData", payload));
        size_t calls = 0;
        CHECK(input_pbf_blocks(path.c_str(), true, [&](const auto&) {
                  ++calls;
                  return true;
              }) == expected);
        CHECK(calls == (expected ? 1u : 0u));
        CHECK(input_file(path.c_str(), true, {}, {}, {}) == expected);
    };
    auto encoded_field = [](uint32_t number, unsigned wire) {
        auto result = varint((uint64_t(number) << 3) | wire);
        if (wire == 0) result += varint(std::numeric_limits<uint64_t>::max());
        if (wire == 1) result += std::string(8, '\0');
        if (wire == 2) result += varint(0);
        if (wire == 3) result += integer(99, 7) + varint((uint64_t(number) << 3) | 4);
        if (wire == 5) result += std::string(4, '\0');
        return result;
    };
    for (unsigned wire : {0, 1, 2, 3, 5})
    {
        const auto unknown = encoded_field(0x1fffffff, wire);
        check_payload(primitive(message(1, node() + unknown)) + unknown, true);
        for (uint32_t number : {1, 8, 9})
            if (wire != 0) check_payload(primitive(message(1, node() + encoded_field(number, wire))), false);
        for (uint32_t number : {2, 3})
            if (wire != 0 && wire != 2)
                check_payload(primitive(message(1, node() + encoded_field(number, wire))), false);
        if (wire != 2) check_payload(primitive(message(1, node() + encoded_field(4, wire))), false);
        for (uint32_t number : {1, 2, 3, 5})
            if (wire != 0)
                check_payload(primitive(message(1, node() + message(4, encoded_field(number, wire)))), false);
    }
    std::string nested;
    for (size_t depth = 0; depth < 64; ++depth) nested += varint((uint64_t(99 + depth) << 3) | 3);
    nested += integer(99, 7);
    for (size_t depth = 64; depth != 0; --depth) nested += varint((uint64_t(98 + depth) << 3) | 4);
    check_payload(primitive(message(1, node() + nested)), true);
    check_payload(primitive(message(1, node() + nested.substr(0, nested.size() - 1))), false);
    for (unsigned wire : {1, 5})
    {
        const auto complete = encoded_field(99, wire);
        for (size_t size = 2; size < complete.size(); ++size)
            check_payload(primitive(message(1, node() + complete.substr(0, size))), false);
    }
    check_payload(primitive(message(1, node() + varint((99u << 3) | 2) + varint(8) + "short")), false);
    check_payload(primitive(message(1, node() + varint(uint64_t{0x20000000} << 3) + varint(0))), false);
}

void cancellation_test(files_t& files)
{
    const auto data = raw_block("OSMData", primitive(message(1, node())));
    std::string file = header();
    for (size_t i = 0; i < 40; ++i) file += data;
    const auto path = files.write(file);
    set_thread_count(1);
    size_t calls = 0;
    CHECK(!input_pbf_blocks(path.c_str(), false, [&](const auto&) {
        ++calls;
        return false;
    }));
    CHECK(calls == 1);
    CHECK(!input_file(path.c_str(), false, [](auto) { return false; }, {}, {}));
    CHECK(!input_file(
        path.c_str(), false, [](auto) -> bool { throw std::runtime_error("Entity callback failure"); }, {}, {}));
    CHECK(!input_pbf_blocks(
        path.c_str(), false, [](const auto&) -> bool { throw std::runtime_error("Callback failure"); }));
    CHECK(!input_pbf_blocks(path.c_str(), false, [](const auto&) -> bool { throw 1; }));
    CHECK(input_pbf_blocks(path.c_str(), false, keep_block));
    set_thread_count(2);
    if (thread_count() < 2) return;
    for (bool fail : {false, true})
    {
        std::mutex mutex;
        std::condition_variable changed;
        size_t entered = 0, exited = 0;
        const auto caller = std::this_thread::get_id();
        CHECK(!input_pbf_blocks(path.c_str(), false, [&](const pbf_block_t& block) {
            std::unique_lock lock(mutex);
            CHECK(std::this_thread::get_id() != caller);
            CHECK(block.nodes[0].id == 1);
            const auto ordinal = ++entered;
            changed.notify_all();
            CHECK(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered == 2; }));
            ++exited;
            if (fail && ordinal == 1) throw std::runtime_error("Concurrent callback failure");
            return false;
        }));
        CHECK(entered == 2 && exited == 2);
        CHECK(input_pbf_blocks(path.c_str(), false, keep_block));
    }
    for (size_t count : {1, 2, 4})
    {
        set_thread_count(count);
        const auto corrupt = files.write(header() + raw_block("OSMData", "invalid") + data);
        CHECK(!input_pbf_blocks(corrupt.c_str(), false, keep_block));
        CHECK(!input_file(corrupt.c_str(), false, {}, {}, {}));
        CHECK(input_pbf_blocks(path.c_str(), false, keep_block));
    }
}
} // namespace

int main()
{
    try
    {
        input_osm::set_log_level(input_osm::LOG_LEVEL_DISABLED);
        files_t files;
        for (bool metadata : {false, true})
            for (size_t threads : {1, 4})
            {
                fixture_test("comprehensive.osm.pbf", 17005, metadata, threads);
                fixture_test("ordinary.osm.pbf", 7, metadata, threads);
            }
        layout_test(files);
        column_test(files);
        dense_limits_test(files);
        growth_test(files);
        error_test(files);
        column_error_test(files);
        reader_boundary_test(files);
        cancellation_test(files);
        std::cout << "PBF tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
