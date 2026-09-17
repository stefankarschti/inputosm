#include <inputosm/inputosm.h>
#include "pbf_test_data.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>

namespace
{
using namespace input_osm;
using namespace pbf_test;
#define CHECK(x)                                                                \
    do                                                                          \
    {                                                                           \
        if (!(x)) throw std::runtime_error(std::to_string(__LINE__) + ": " #x); \
    } while (false)

static_assert(!std::is_copy_constructible_v<pbf_block_t>);
static_assert(!std::is_move_constructible_v<pbf_block_t>);
static_assert(std::is_invocable_r_v<bool, pbf_string_handler_t, std::string_view>);
static_assert(!std::is_invocable_v<pbf_string_handler_t, uint32_t, std::string_view>);

std::string info()
{
    return integer(1, 7) + integer(2, 5000000000LL) + integer(3, 6000000000LL) + integer(4, 42) + integer(5, 999) +
           integer(6, 0);
}
std::string payload()
{
    const auto tags = packed(2, {1}) + packed(3, {2});
    const auto nodes = message(1, node(10) + tags + message(4, info())) + message(1, node(11));
    const auto dense_info = packed(1, {7, 8}) + packed(2, {sint(5000000000LL), sint(-1)}) +
                            packed(3, {sint(6000000000LL), sint(2)}) + packed(4, {sint(42), sint(-1)}) +
                            packed(5, {sint(999), sint(-1)}) + packed(6, {0, 1});
    const auto dense = packed(1, {sint(20)}) + integer(1, sint(-1)) + packed(8, {sint(-3), sint(4)}) +
                       packed(9, {sint(5), sint(-2)}) + packed(10, {1, 2, 0, 0}) + message(5, dense_info);
    const auto way = integer(1, 30) + tags + message(4, info()) + packed(8, {sint(10), sint(1), sint(-2)}) +
                     packed(9, {sint(1), sint(2), sint(-4)}) + packed(10, {sint(2), sint(-1), sint(3)});
    const auto relation = integer(1, 40) + tags + message(4, info()) + packed(8, {1, 999, 0}) +
                          packed(9, {sint(10), sint(-3), sint(5)}) + packed(10, {0, 1, 2});
    return table({"", "key", "value"}) + message(2, nodes) + message(2, message(2, dense)) +
           message(2, message(3, way) + message(3, integer(1, 31))) + message(2, message(4, relation)) +
           integer(17, 10) + integer(18, 2000) + integer(19, 3) + integer(20, 4);
}
void metadata(const pbf_metadata_t& value)
{
    CHECK(value.present == 63 && value.version == 7 && value.raw_timestamp == 5000000000LL);
    CHECK(value.changeset == 6000000000LL && value.uid == 42 && value.user_sid == 999 && !value.visible);
}
void combinations(files_t& files)
{
    for (bool compressed : {false, true})
    {
        const auto path = files.write(header() + raw_block("Ignored", "x") +
                                      (compressed ? zlib_block(payload()) : raw_block("OSMData", payload())));
        // Location decoding requires its header feature declaration.
        const auto location_path = files.write(
            raw_block("OSMHeader", message(4, "OsmSchema-V0.6") + message(5, "LocationsOnWays")) +
            (compressed ? zlib_block(payload()) : raw_block("OSMData", payload())));
        pbf_reader_t reader;
        CHECK(reader.open(location_path.c_str()));
        CHECK(reader.read_block(1, [&](const pbf_block_t& block) {
            size_t count = 100;
            CHECK(block.string_table_size(count) && count == 3);
            std::vector<std::string_view> strings;
            CHECK(block.decode_strings([&](auto value) {
                strings.push_back(value);
                return true;
            }));
            CHECK(strings.size() == 3 && strings[0].empty() && strings[1] == "key" && strings[2] == "value");
            CHECK(block.node_count(count) && count == 4);
            CHECK(block.way_count(count) && count == 2);
            CHECK(block.relation_count(count) && count == 1);
            pbf_counts_t counts;
            CHECK(block.counts(counts) && counts.nodes == 4 && counts.ways == 2 && counts.relations == 1);
            pbf_parameters_t params;
            CHECK(block.parameters(params) && params.granularity == 10 && params.date_granularity == 2000);
            CHECK(params.lat_offset == 3 && params.lon_offset == 4);
            const auto caller = std::this_thread::get_id();
            for (unsigned mask = 0; mask < 32; ++mask)
            {
                const pbf_node_options_t fields{
                    bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8), bool(mask & 16)};
                size_t offset = 0, calls = 0;
                CHECK(block.decode_nodes(fields, [&](const pbf_node_batch_t& batch) {
                    CHECK(std::this_thread::get_id() == caller);
                    CHECK(batch.block_index == 1 && batch.group_index == calls++ && batch.count == 2);
                    CHECK(batch.ids.size() == (fields.id ? 2u : 0u));
                    CHECK(batch.raw_latitudes.size() == (fields.latitude ? 2u : 0u));
                    CHECK(batch.raw_longitudes.size() == (fields.longitude ? 2u : 0u));
                    CHECK(batch.tags.offsets.size() == (fields.tags ? 3u : 0u));
                    CHECK(batch.metadata.size() == (fields.metadata ? 2u : 0u));
                    const std::array<int64_t, 4> ids{10, 11, 20, 19}, lat{-100, -100, -3, 1}, lon{200, 200, 5, 3};
                    for (size_t i = 0; i < batch.count; ++i)
                    {
                        if (fields.id) CHECK(batch.ids[i] == ids[offset + i]);
                        if (fields.latitude) CHECK(batch.raw_latitudes[i] == lat[offset + i]);
                        if (fields.longitude) CHECK(batch.raw_longitudes[i] == lon[offset + i]);
                    }
                    if (fields.tags)
                        CHECK(batch.tags[0].size() == 1 && batch.tags[0][0].key == 1 && batch.tags[1].empty());
                    if (fields.metadata)
                    {
                        metadata(batch.metadata[0]);
                        if (!offset)
                            CHECK(batch.metadata[1].present == 0 && batch.metadata[1].version == -1);
                        else
                            CHECK(batch.metadata[1].raw_timestamp == 4999999999LL && batch.metadata[1].user_sid == 998);
                    }
                    offset += batch.count;
                    return true;
                }));
                CHECK(offset == 4 && calls == 2);
            }
            for (unsigned mask = 0; mask < 16; ++mask)
            {
                pbf_way_options_t fields{bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8)};
                size_t calls = 0;
                CHECK(block.decode_ways(fields, [&](const pbf_way_batch_t& batch) {
                    CHECK(++calls == 1 && batch.count == 2 && batch.ids[0] == 30 && batch.ids[1] == 31);
                    CHECK(batch.tags.offsets.size() == (fields.tags ? 3u : 0u));
                    CHECK(batch.node_refs.offsets.size() == (fields.node_refs ? 3u : 0u));
                    CHECK(batch.node_locations.offsets.size() == (fields.node_locations ? 3u : 0u));
                    CHECK(batch.metadata.size() == (fields.metadata ? 2u : 0u));
                    if (fields.tags) CHECK(batch.tags[0][0].value == 2 && batch.tags[1].empty());
                    if (fields.node_refs)
                        CHECK(batch.node_refs[0][0] == 10 && batch.node_refs[0][2] == 9 && batch.node_refs[1].empty());
                    if (fields.node_locations)
                    {
                        CHECK(batch.locations_present[0] == 1 && batch.locations_present[1] == 0);
                        CHECK(batch.node_locations[0][2].raw_latitude == -1 &&
                              batch.node_locations[0][2].raw_longitude == 4);
                    }
                    if (fields.metadata) metadata(batch.metadata[0]);
                    return true;
                }));
            }
            for (unsigned mask = 0; mask < 32; ++mask)
            {
                pbf_relation_options_t fields{
                    bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8), bool(mask & 16)};
                CHECK(block.decode_relations(fields, [&](const pbf_relation_batch_t& batch) {
                    CHECK(batch.count == 1 && batch.ids[0] == 40 && batch.group_index == 3);
                    CHECK(batch.tags.offsets.size() == (fields.tags ? 2u : 0u));
                    CHECK(batch.member_offsets.size() == ((mask & 14) ? 2u : 0u));
                    CHECK(batch.member_ids.size() == (fields.member_ids ? 3u : 0u));
                    CHECK(batch.member_types.size() == (fields.member_types ? 3u : 0u));
                    CHECK(batch.member_roles.size() == (fields.member_roles ? 3u : 0u));
                    CHECK(batch.metadata.size() == (fields.metadata ? 1u : 0u));
                    if (fields.member_ids) CHECK(batch.member_ids[1] == 7 && batch.member_ids[2] == 12);
                    if (fields.member_types) CHECK(batch.member_types[2] == pbf_member_type_t::relation);
                    if (fields.member_roles) CHECK(batch.member_roles[1] == 999);
                    if (fields.metadata) metadata(batch.metadata[0]);
                    return true;
                }));
            }
            CHECK(strings[1] == "key");
            CHECK(block.validate());
            return true;
        }));
        CHECK(reader.read_block(1, [](const pbf_block_t& block) {
            CHECK(block.decode_nodes({false, false, false, false, false}, [](const auto& batch) {
                CHECK(batch.count == 2 && batch.ids.empty());
                return true;
            }));
            pbf_counts_t counts;
            CHECK(block.counts(counts) && counts.nodes == 4 && counts.ways == 2 && counts.relations == 1);
            CHECK(block.decode_ways({}, [](const auto& batch) { return batch.count == 2; }));
            CHECK(block.counts(counts) && counts.nodes == 4 && counts.ways == 2 && counts.relations == 1);
            return true;
        }));
        CHECK(reader.build_index());
        CHECK(reader.read_block(1, [](const auto& block) { return block.validate(); }));
        pbf_reader_t no_locations;
        CHECK(no_locations.open(path.c_str()));
        CHECK(no_locations.read_block(
            2, [](const auto& block) { return block.decode_ways({}, [](const auto&) { return true; }); }));
        CHECK(!no_locations.read_block(2, [](const auto& block) { return block.validate(); }));
    }
}
void headers(files_t& files)
{
    const auto bbox = integer(1, sint(-10)) + integer(2, sint(20)) + integer(3, sint(30)) + integer(4, sint(-40));
    const auto bytes = message(1, bbox) + message(4, "OsmSchema-V0.6") + message(4, "DenseNodes") +
                       message(5, "LocationsOnWays") + message(5, "Unknown") + message(16, "") + message(17, "source") +
                       integer(32, 0) + integer(33, INT64_MAX) + message(34, "https://example.test/replication");
    for (bool compressed : {false, true})
    {
        const auto path = files.write(
            compressed ? file_block("OSMHeader", integer(2, bytes.size()) + message(3, pbf_test::compressed(bytes)))
                       : raw_block("OSMHeader", bytes));
        pbf_reader_t reader;
        CHECK(reader.open(path.c_str()));
        const auto caller = std::this_thread::get_id();
        CHECK(reader.decode_header([&](const pbf_header_metadata_t& value) {
            CHECK(std::this_thread::get_id() == caller);
            CHECK(value.bbox && value.bbox->left == -10 && value.bbox->bottom == -40);
            CHECK(value.required_features.size() == 2 && value.optional_features.size() == 2);
            CHECK(value.writingprogram && value.writingprogram->empty() && value.source == "source");
            CHECK(value.osmosis_replication_timestamp == 0 && value.osmosis_replication_sequence_number == INT64_MAX);
            CHECK(value.osmosis_replication_base_url == "https://example.test/replication");
            CHECK(!reader.decode_header([](const auto&) { return true; }));
            return true;
        }));
        CHECK(!reader.decode_header({}));
        CHECK(!reader.decode_header([](const auto&) -> bool { throw std::runtime_error("stop"); }));
        CHECK(reader.read_blocks([](const auto&) { return false; }));
    }
    pbf_reader_t reader;
    CHECK(reader.open(files.write(raw_block("OSMHeader", "")).c_str()));
    CHECK(reader.decode_header([](const auto& value) {
        CHECK(!value.bbox && !value.source && !value.osmosis_replication_timestamp && value.required_features.empty());
        return true;
    }));
    reader.close();
    const auto split_bbox = message(1, integer(1, sint(-10)) + integer(2, sint(20))) +
                            message(1, integer(3, sint(30)) + integer(4, sint(-40)));
    CHECK(reader.open(files.write(raw_block("OSMHeader", split_bbox)).c_str()));
    CHECK(reader.decode_header([](const auto& value) {
        CHECK(value.bbox && value.bbox->left == -10 && value.bbox->right == 20);
        CHECK(value.bbox->top == 30 && value.bbox->bottom == -40);
        return true;
    }));
}
void deferred_and_failures(files_t& files)
{
    pbf_reader_t reader;
    CHECK(reader.open(files.write(header() + file_block("OSMData", integer(2, 100) + message(3, "broken"))).c_str()));
    CHECK(reader.read_block(1, [](const auto&) { return true; }));
    size_t count = 123;
    CHECK(!reader.read_block(1, [&](const auto& block) {
        CHECK(!block.node_count(count));
        return true;
    }));
    CHECK(count == 123);
    reader.close();
    const auto unparsed = table({""}) + message(2, message(1, node() + packed(2, {999}) + packed(3, {1000})));
    CHECK(reader.open(files.write(header() + raw_block("OSMData", unparsed)).c_str()));
    CHECK(reader.read_block(1, [](const auto& block) {
        return block.decode_nodes({false, false, false, true, false}, [](const auto& batch) {
            CHECK(batch.tags[0][0].key == 999 && batch.tags[0][0].value == 1000);
            return true;
        });
    }));
    CHECK(reader.read_block(1, [](const auto& block) { return block.validate(); }));
    CHECK(!reader.read_block(1, [](const auto& block) {
        CHECK(!block.decode_strings([](auto) { return false; }));
        return true;
    }));
    CHECK(!reader.read_block(1, [](const auto& block) {
        return block.decode_nodes({}, [&](const auto&) {
            size_t value;
            return block.node_count(value);
        });
    }));
    CHECK(reader.read_block(1, [](const auto& block) { return block.validate(); }));
    reader.close();
    // Unrequested string data does not prevent entity decoding.
    CHECK(reader.open(
        files.write(header() + raw_block("OSMData", message(1, "broken") + message(2, message(1, node())))).c_str()));
    CHECK(reader.read_block(1, [](const auto& block) {
        return block.decode_nodes({}, [](const auto& batch) { return batch.ids[0] == 1; });
    }));
    CHECK(!reader.read_block(1, [](const auto& block) {
        size_t value;
        return block.string_table_size(value);
    }));
}
void selected_validation(files_t& files)
{
    auto read = [&](std::string_view group, const pbf_block_handler_t& handler) {
        pbf_reader_t reader;
        const auto path = files.write(header() + raw_block("OSMData", primitive(group)));
        return reader.open(path.c_str()) && reader.read_block(1, handler);
    };
    for (uint32_t field : {1, 2, 3, 4})
        CHECK(!read(integer(field, 0), [](const auto& block) {
            pbf_counts_t counts{11, 12, 13};
            CHECK(!block.counts(counts));
            CHECK(counts.nodes == 11 && counts.ways == 12 && counts.relations == 13);
            return true;
        }));
    for (auto malformed : {std::string(1, char(0x80)), std::string(9, char(0xff)) + char(2)})
        CHECK(!read(message(2, message(1, malformed)), [](const auto& block) {
            size_t count = 17;
            CHECK(!block.node_count(count) && count == 17);
            return true;
        }));
    for (size_t prefix = 0; prefix < 16; ++prefix)
    {
        std::string values(prefix, char(1));
        size_t expected = prefix;
        for (unsigned bits = 0; bits < 64; ++bits)
        {
            values += varint(uint64_t{1} << bits);
            ++expected;
        }
        values += varint(UINT64_MAX);
        ++expected;
        CHECK(read(message(2, message(1, values)), [&](const auto& block) {
            size_t count;
            return block.node_count(count) && count == expected;
        }));
    }
    const auto invalid_tags = message(1, node() + packed(2, {1}));
    CHECK(
        read(invalid_tags, [](const auto& block) { return block.decode_nodes({}, [](const auto&) { return true; }); }));
    CHECK(!read(invalid_tags, [](const auto& block) { return block.validate(); }));
    const auto invalid_positions = message(2, packed(1, {2}) + message(8, std::string(1, char(0x80))) + packed(9, {2}));
    CHECK(read(invalid_positions, [](const auto& block) {
        size_t count;
        return block.node_count(count) && count == 1;
    }));
    CHECK(read(invalid_positions,
               [](const auto& block) { return block.decode_nodes({}, [](const auto&) { return true; }); }));
    CHECK(!read(invalid_positions, [](const auto& block) { return block.validate(); }));
    const auto invalid_members = message(4, integer(1, 1) + packed(8, {1}) + packed(9, {2, 2}) + packed(10, {0}));
    CHECK(read(invalid_members,
               [](const auto& block) { return block.decode_relations({}, [](const auto&) { return true; }); }));
    CHECK(!read(invalid_members, [](const auto& block) {
        return block.decode_relations({false, false, false, true, false}, [](const auto&) { return true; });
    }));
    const auto invalid_type = message(4, integer(1, 1) + packed(8, {0}) + packed(9, {2}) + packed(10, {99}));
    CHECK(read(invalid_type, [](const auto& block) {
        return block.decode_relations({false, true, false, false, false}, [](const auto&) { return true; });
    }));
    CHECK(!read(invalid_type, [](const auto& block) { return block.validate(); }));
    const auto invalid_way = message(3, integer(1, 1) + packed(8, {2, 2}) + packed(9, {2}) + packed(10, {2}));
    CHECK(read(invalid_way, [](const auto& block) { return block.decode_ways({}, [](const auto&) { return true; }); }));
    CHECK(!read(invalid_way, [](const auto& block) {
        return block.decode_ways({false, false, false, true}, [](const auto&) { return true; });
    }));
}
void threads_and_nesting(files_t& files)
{
    const auto data = primitive(message(1, node(17)));
    const auto path = files.write(header() + zlib_block(data) + raw_block("OSMData", data));
    std::string inner_nodes;
    for (size_t i = 0; i < 513; ++i) inner_nodes += message(1, node(1000 + static_cast<int64_t>(i)));
    const auto inner_path = files.write(header() + zlib_block(table({"", "different"}) + message(2, inner_nodes)));
    pbf_reader_t first, second;
    CHECK(first.open(path.c_str()) && second.open(inner_path.c_str()));
    first.set_thread_count(4);
    std::atomic<size_t> calls{0};
    CHECK(first.read_blocks([&](const auto& outer) {
        const auto caller = std::this_thread::get_id();
        return outer.decode_nodes({}, [&](const auto& batch) {
            CHECK(std::this_thread::get_id() == caller && batch.ids[0] == 17);
            ++calls;
            return true;
        });
    }));
    CHECK(calls == 2);
    CHECK(first.read_block(1, [&](const auto& outer) {
        size_t string_index = 0;
        std::string_view outer_name;
        CHECK(outer.decode_strings([&](auto value) {
            if (string_index++ == 1) outer_name = value;
            return true;
        }));
        return outer.decode_nodes({}, [&](const auto& batch) {
            const auto* saved = batch.ids.data();
            CHECK(second.read_block(1, [](const auto& inner) { return inner.validate(); }));
            CHECK(batch.ids.data() == saved && batch.ids[0] == 17);
            CHECK(outer_name == "name");
            return true;
        });
    }));
}
} // namespace
int main()
{
    try
    {
        input_osm::set_log_level(input_osm::LOG_LEVEL_DISABLED);
        files_t files;
        combinations(files);
        headers(files);
        deferred_and_failures(files);
        selected_validation(files);
        threads_and_nesting(files);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
