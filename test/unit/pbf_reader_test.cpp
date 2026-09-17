#include <inputosm/inputosm.hpp>
#include "pbf_test_data.hpp"
#include "pbf_eager_test_view.hpp"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <map>
#include <mutex>
#include <thread>
#include <type_traits>

namespace
{
using namespace input_osm;
using namespace pbf_test;

void check(bool result, const char* expression, int line)
{
    if (!result) throw std::runtime_error(std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)

static_assert(!std::is_copy_constructible_v<input_osm::pbf_reader_t>);
static_assert(std::is_nothrow_move_constructible_v<input_osm::pbf_reader_t>);
static_assert(std::is_nothrow_move_assignable_v<input_osm::pbf_reader_t>);

bool keep(const eager_block_t&)
{
    return true;
}

std::string snapshot(const eager_block_t& block)
{
    std::string result;
    auto number = [&](auto value) {
        result += std::to_string(value) + ";";
    };
    auto string = [&](std::string_view value) {
        number(value.size());
        result.append(value);
    };
    auto entity = [&](const auto& value) {
        number(value.id);
        number(value.version);
        number(value.timestamp);
        number(value.changeset);
        number(value.tags.size());
        for (const auto& tag : value.tags)
        {
            string(tag.key);
            string(tag.value);
        }
    };
    number(block.index);
    number(block.file_offset);
    number(block.granularity);
    number(block.date_granularity);
    number(block.lat_offset);
    number(block.lon_offset);
    number(block.string_table.size());
    for (auto value : block.string_table) string(value);
    number(block.nodes.size());
    for (const auto& value : block.nodes)
    {
        entity(value);
        number(value.raw_latitude);
        number(value.raw_longitude);
    }
    number(block.ways.size());
    for (const auto& value : block.ways)
    {
        entity(value);
        number(value.node_refs.size());
        for (auto reference : value.node_refs) number(reference);
    }
    number(block.relations.size());
    for (const auto& value : block.relations)
    {
        entity(value);
        number(value.members.size());
        for (const auto& member : value.members)
        {
            number(member.type);
            number(member.id);
            string(member.role);
        }
    }
    return result;
}

void lifetime_test(files_t& files)
{
    const auto path = files.write(header() + raw_block("OSMData", primitive(message(1, node(17)))));
    eager_reader_t reader;
    CHECK(!reader.is_open() && !reader.has_index() && reader.index_memory_bytes() == 0);
    CHECK(reader.thread_count() == 1);
    CHECK(!reader.read_block(1, false, keep) && !reader.read_blocks(false, keep));
    CHECK(!reader.build_index());
    CHECK(!reader.open(nullptr));
    CHECK(!reader.open("/inputosm-missing-random-input"));
    CHECK(!reader.open(files.write("<osm/>").c_str()));
    reader.set_thread_count(0);
    CHECK(reader.thread_count() == 1);
    reader.set_thread_count(std::numeric_limits<size_t>::max());
    const auto maximum = std::max<size_t>(1, std::thread::hardware_concurrency());
    CHECK(reader.thread_count() == maximum);
    reader.set_max_thread_count();
    CHECK(reader.thread_count() == maximum);
    CHECK(reader.open(path.c_str()));
    CHECK(!reader.open(nullptr) && reader.is_open());
    CHECK(reader.read_block(1, false, keep));
    CHECK(reader.build_index() && reader.has_index());
    const auto index_bytes = reader.index_memory_bytes();
    CHECK(index_bytes >= 2 * sizeof(uint64_t));
    CHECK(reader.build_index() && reader.index_memory_bytes() == index_bytes);
    eager_reader_t moved(std::move(reader));
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move): The move contract leaves a closed reader.
    CHECK(!reader.is_open() && reader.thread_count() == 1 && !reader.has_index());
    CHECK(moved.is_open() && moved.has_index() && moved.thread_count() == maximum);
    CHECK(reader.open(path.c_str()));
    reader = std::move(moved);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move): The move contract leaves a closed reader.
    CHECK(!moved.is_open() && moved.thread_count() == 1);
    CHECK(reader.has_index() && reader.read_block(1, false, keep));
    reader.close();
    reader.close();
    CHECK(!reader.is_open() && !reader.has_index() && reader.index_memory_bytes() == 0);
    CHECK(reader.thread_count() == maximum);
    CHECK(reader.open(files.write(header()).c_str()));
    size_t calls = 0;
    CHECK(reader.read_blocks(false, [&](const auto&) {
        ++calls;
        return true;
    }));
    CHECK(calls == 0 && reader.build_index());
    CHECK(!reader.read_block(0, false, keep) && !reader.read_block(1, false, keep));
}

void access_test(files_t& files)
{
    const auto info = message(4, integer(1, 7) + integer(2, 123) + integer(3, 456));
    const auto dense = packed(1, {sint(18), sint(-2)}) + packed(8, {sint(-1), sint(2)}) +
                       packed(9, {sint(4), sint(-3)});
    const auto payload = table({"", "key", std::string_view("a\0b", 3), "role", "unused"}) +
                         message(2, message(3, integer(1, 90) + packed(8, {sint(18), sint(-2)}))) +
                         message(2, message(1, node(11) + packed(2, {1}) + packed(3, {2}) + info)) +
                         message(
                             2, message(4, integer(1, 91) + packed(8, {3}) + packed(9, {sint(90)}) + packed(10, {1}))) +
                         message(2, message(2, dense)) + integer(17, 10) + integer(18, 100);
    const auto bytes = header() + raw_block("OSMData", payload) + file_block("Unknown", "opaque") +
                       zlib_block(payload) + raw_block("OSMData", table());
    const auto path = files.write(bytes, ".binary");
    for (bool indexed : {false, true})
    {
        eager_reader_t reader;
        reader.set_thread_count(4);
        CHECK(reader.open(path.c_str()));
        if (indexed) CHECK(reader.build_index());
        const auto worker_count = reader.thread_count();
        file_type = file_type_t::xml;
        osc_mode = input_osm::mode_t::modify;
        block_index = 1234;
        thread_index = 5678;
        for (bool metadata : {true, false, true})
        {
            std::map<size_t, std::string> expected;
            std::mutex mutex;
            CHECK(reader.read_blocks(metadata, [&](const auto& block) {
                CHECK(thread_index < worker_count);
                CHECK(block_index == block.index);
                std::lock_guard lock(mutex);
                return expected.emplace(block.index, snapshot(block)).second;
            }));
            CHECK(expected.size() == 3 && expected.contains(1) && expected.contains(3) && expected.contains(4));
            const auto caller = std::this_thread::get_id();
            for (size_t index : {4, 3, 1, 3, 1})
                CHECK(reader.read_block(index, metadata, [&](const auto& block) {
                    CHECK(std::this_thread::get_id() == caller);
                    CHECK(thread_index == 0 && block_index == index && block.index == index);
                    CHECK(snapshot(block) == expected.at(index));
                    return true;
                }));
            CHECK(block_index == 1234 && thread_index == 5678);
            CHECK(file_type == file_type_t::xml && osc_mode == input_osm::mode_t::modify);
        }
        for (size_t index : {size_t{0}, size_t{2}, size_t{5}, std::numeric_limits<size_t>::max()})
            CHECK(!reader.read_block(index, false, keep));
        CHECK(!reader.read_block(1, false, {}));
        CHECK(!reader.read_blocks(false, {}));
        CHECK(reader.read_block(1, false, keep));
    }
}

void failure_test(files_t& files)
{
    const auto good = raw_block("OSMData", primitive(message(1, node(23))));
    const auto path = files.write(header() + raw_block("OSMData", "invalid") + good);
    for (bool indexed : {false, true})
    {
        eager_reader_t reader;
        CHECK(reader.open(path.c_str()));
        if (indexed) CHECK(reader.build_index());
        CHECK(reader.read_block(2, false, keep));
        CHECK(!reader.read_block(1, false, keep));
        CHECK(reader.read_block(2, false, keep));
        for (int failure : {0, 1, 2})
        {
            block_index = 99;
            thread_index = 88;
            CHECK(!reader.read_block(2, false, [&](const auto&) -> bool {
                if (failure == 1) throw std::runtime_error("Handler failure");
                if (failure == 2) throw 7;
                return false;
            }));
            CHECK(block_index == 99 && thread_index == 88);
            CHECK(reader.read_block(2, false, keep));
        }
        CHECK(!reader.read_blocks(false, keep));
        CHECK(reader.read_block(2, false, keep));
    }
    for (const auto& invalid : {std::string("bad"), header(), prefix(65536), prefix(0)})
    {
        eager_reader_t reader;
        CHECK(reader.open(files.write(header() + good + invalid).c_str()));
        CHECK(reader.read_block(1, false, keep));
        CHECK(!reader.read_block(2, false, keep));
        CHECK(!reader.build_index() && !reader.has_index() && reader.index_memory_bytes() == 0);
        CHECK(reader.read_block(1, false, keep));
        CHECK(!reader.read_blocks(false, keep));
    }
    eager_reader_t reader;
    CHECK(!reader.open(files.write(header("Unsupported")).c_str()));
    const auto first = header();
    for (size_t size = 0; size < first.size(); ++size) CHECK(!reader.open(files.write(first.substr(0, size)).c_str()));
}

void adapter_test(files_t& files)
{
    const auto payload = table() + message(2, message(3, integer(1, 2))) + message(2, message(1, node(1))) +
                         message(2, message(1, node(3))) + message(2, message(4, integer(1, 4)));
    const auto path = files.write(header() + raw_block("OSMData", payload) + raw_block("OSMData", table()));
    set_thread_count(1);
    std::vector<std::pair<char, size_t>> calls;
    auto record = [&](char type, auto entities) {
        CHECK(file_type == file_type_t::pbf && osc_mode == input_osm::mode_t::bulk);
        CHECK(thread_index == 0 && block_index == 1);
        calls.emplace_back(type, entities.size());
        return true;
    };
    CHECK(input_file(
        path.c_str(),
        false,
        [&](auto nodes) { return record('n', nodes); },
        [&](auto ways) { return record('w', ways); },
        [&](auto relations) { return record('r', relations); }));
    CHECK((calls == std::vector<std::pair<char, size_t>>{{'w', 1}, {'n', 1}, {'n', 1}, {'r', 1}}));
    CHECK(input_file(path.c_str(), false, {}, {}, {}));
    for (bool throws : {false, true})
        for (int stop : {0, 1, 2, 3})
        {
            size_t count = 0;
            auto handler = [&](auto entities) {
                CHECK(!entities.empty());
                if (count++ != static_cast<size_t>(stop)) return true;
                if (throws) throw std::runtime_error("Entity handler failure");
                return false;
            };
            CHECK(!input_file(path.c_str(), false, handler, handler, handler));
            CHECK(count == static_cast<size_t>(stop + 1));
        }
    size_t ways = 0;
    CHECK(input_file(path.c_str(),
                     false,
                     {},
                     [&](auto entities) {
                         CHECK(entities.size() == 1);
                         ways += entities.size();
                         return true;
                     },
                     {}));
    CHECK(ways == 1);
    const auto corrupt = files.write(header() + raw_block("OSMData", payload + message(2, "invalid")));
    size_t partial = 0;
    CHECK(!input_file(corrupt.c_str(),
                      false,
                      [&](auto) {
                          ++partial;
                          return true;
                      },
                      {},
                      {}));
    CHECK(partial == 2);
}

void adapter_nonempty_test(files_t& files)
{
    const auto empty_dense = packed(1, {}) + packed(8, {}) + packed(9, {});
    const auto empty_groups = message(2, "") + message(2, message(2, empty_dense));
    const auto ordinary = message(2, message(1, node(1)) + message(1, node(2)));
    const auto dense = message(2, message(2, packed(1, {sint(3), sint(1)}) + packed(8, {0, 0}) + packed(9, {0, 0})));
    const auto ways = message(2, message(3, integer(1, 5)) + message(3, integer(1, 6)));
    const auto relations = message(2, message(4, integer(1, 7)) + message(4, integer(1, 8)));
    struct case_t
    {
        std::string groups;
        size_t node_calls, way_calls, relation_calls;
    };
    const std::vector<case_t> cases{
        {"", 0, 0, 0},
        {ordinary, 1, 0, 0},
        {dense, 1, 0, 0},
        {ways, 0, 1, 0},
        {relations, 0, 0, 1},
        {ordinary + empty_groups + dense + empty_groups + ways + empty_groups + relations, 2, 1, 1}};
    for (const bool compressed : {false, true})
    {
        const auto block = [&](const std::string& payload) {
            return compressed ? zlib_block(payload) : raw_block("OSMData", payload);
        };
        for (const auto& test : cases)
        {
            const auto path = files.write(header() + block(table()) +
                                          block(table() + empty_groups + test.groups + empty_groups) +
                                          block(table() + empty_groups));
            for (const bool metadata : {false, true})
                for (const size_t threads : {1, 4})
                {
                    set_thread_count(threads);
                    std::atomic<size_t> node_calls{0}, way_calls{0}, relation_calls{0};
                    const auto record = [](auto entities, auto& calls) {
                        CHECK(entities.size() == 2);
                        ++calls;
                        return true;
                    };
                    CHECK(input_file(
                        path.c_str(),
                        metadata,
                        [&](auto entities) { return record(entities, node_calls); },
                        [&](auto entities) { return record(entities, way_calls); },
                        [&](auto entities) { return record(entities, relation_calls); }));
                    CHECK(node_calls == test.node_calls);
                    CHECK(way_calls == test.way_calls);
                    CHECK(relation_calls == test.relation_calls);
                }
        }
    }
    const auto path = files.write(header());
    const auto reject_callback = [](auto) {
        return false;
    };
    CHECK(input_file(path.c_str(), false, reject_callback, reject_callback, reject_callback));
    set_thread_count(1);
}

void descriptor_test(files_t& files)
{
    const auto blob = message(1, primitive(message(1, node(42))));
    const auto type = message(1, "OSMData");
    const auto size = integer(3, blob.size());
    const std::vector<std::string> headers{
        type + size,
        size + type,
        type + size + message(2, "optional index data"),
        type + integer(3, 0) + size,
        type + integer(3, std::numeric_limits<uint64_t>::max()) + size,
        message(1, "Unknown") + type + size,
        type + size + integer(99, 123),
        type + size + varint((99u << 3) | 3) + integer(1, 7) + varint((99u << 3) | 4)};
    for (const auto& descriptor : headers)
    {
        const auto path = files.write(header() + prefix(descriptor.size()) + descriptor + blob);
        eager_reader_t reader;
        CHECK(reader.open(path.c_str()));
        const auto handler = [](const eager_block_t& block) {
            CHECK(block.index == 1 && block.nodes.size() == 1 && block.nodes[0].id == 42);
            return true;
        };
        CHECK(reader.read_block(1, false, handler));
        CHECK(reader.read_blocks(false, handler));
        CHECK(reader.build_index());
        CHECK(reader.read_block(1, false, handler));
    }
    const auto unknown = type + size + message(1, "Unknown");
    eager_reader_t skipped;
    CHECK(skipped.open(files.write(header() + prefix(unknown.size()) + unknown + blob).c_str()));
    size_t calls = 0;
    CHECK(skipped.read_blocks(false, [&](const auto&) {
        ++calls;
        return true;
    }));
    CHECK(calls == 0 && !skipped.read_block(1, false, keep));
    CHECK(skipped.build_index() && !skipped.read_block(1, false, keep));

    const std::vector<std::string> invalid{type,
                                           type + message(3, "invalid"),
                                           type + integer(3, uint64_t{1} << 31),
                                           type + integer(3, std::numeric_limits<uint64_t>::max()),
                                           type + std::string("\x18\x80", 2),
                                           type + size + std::string("\0", 1)};
    for (const auto& descriptor : invalid)
    {
        eager_reader_t reader;
        CHECK(reader.open(files.write(header() + prefix(descriptor.size()) + descriptor + blob).c_str()));
        CHECK(!reader.read_block(1, false, keep));
        CHECK(!reader.read_blocks(false, keep));
        CHECK(!reader.build_index() && !reader.has_index());
    }
}

void concurrent_test(files_t& files)
{
    const auto payload = primitive(message(1, node(77) + message(4, integer(1, 9))));
    const auto path = files.write(header() + zlib_block(payload));
    for (bool first_all : {false, true})
        for (bool second_all : {false, true})
        {
            std::mutex mutex;
            std::condition_variable changed;
            size_t entered = 0;
            bool failed_done = false;
            std::atomic<bool> passed{true};
            auto operation = [&](size_t worker, bool all) {
                try
                {
                    eager_reader_t reader;
                    reader.set_thread_count(worker + 1);
                    CHECK(reader.open(path.c_str()));
                    if (worker == 1) CHECK(reader.build_index());
                    block_index = 111;
                    thread_index = 222;
                    const auto handler = [&](const eager_block_t& block) {
                        const auto before = snapshot(block);
                        CHECK(block.nodes[0].version == (worker == 0 ? 9 : 0));
                        std::unique_lock lock(mutex);
                        ++entered;
                        changed.notify_all();
                        CHECK(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered == 2; }));
                        if (worker == 0) return false;
                        CHECK(changed.wait_for(lock, std::chrono::seconds(5), [&] { return failed_done; }));
                        CHECK(snapshot(block) == before);
                        return true;
                    };
                    const bool result = all ? reader.read_blocks(worker == 0, handler)
                                            : reader.read_block(1, worker == 0, handler);
                    CHECK(result == (worker == 1));
                    CHECK(block_index == 111 && thread_index == 222);
                    if (worker == 0)
                    {
                        std::lock_guard lock(mutex);
                        failed_done = true;
                        changed.notify_all();
                    }
                }
                catch (...)
                {
                    passed.store(false);
                }
            };
            std::thread first(operation, 0, first_all);
            std::thread second(operation, 1, second_all);
            first.join();
            second.join();
            CHECK(passed.load() && entered == 2 && failed_done);
        }
}

void large_offset_test(files_t& files)
{
    if constexpr (sizeof(size_t) < 8) return;
    const auto path = files.write(header());
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(0, std::ios::end);
    for (size_t i = 0; i < 2; ++i)
    {
        const auto blob_header = message(1, "Unknown") + integer(3, std::numeric_limits<int32_t>::max());
        const auto framing = prefix(blob_header.size()) + blob_header;
        file.write(framing.data(), framing.size());
        file.seekp(std::numeric_limits<int32_t>::max(), std::ios::cur);
    }
    const auto offset = static_cast<uint64_t>(file.tellp());
    const auto data = raw_block("OSMData", primitive(message(1, node(42))));
    file.write(data.data(), data.size());
    file.close();
    CHECK(!file.fail() && offset > (uint64_t{1} << 32));
    eager_reader_t reader;
    CHECK(reader.open(path.c_str()));
    const auto handler = [&](const eager_block_t& block) {
        CHECK(block.index == 3 && block.file_offset == offset && block.nodes[0].id == 42);
        return true;
    };
    CHECK(reader.read_block(3, false, handler));
    CHECK(reader.build_index());
    CHECK(reader.read_block(3, false, handler));
}

void shared_file_test(files_t& files)
{
    const auto path = files.write(header() + raw_block("OSMData", primitive(message(1, node(77)))));
    eager_reader_t first, second;
    CHECK(first.open(path.c_str()) && second.open(path.c_str()));
    CHECK(first.read_block(1, false, [&](const eager_block_t& outer) {
        return second.read_block(1, false, [&](const eager_block_t& inner) {
#ifndef INPUTOSM_BENCH_INDEPENDENT_MAPS
            CHECK(outer.string_table[0].data() == inner.string_table[0].data());
#endif
            CHECK(outer.nodes[0].id == inner.nodes[0].id);
            return true;
        });
    }));
    first.close();
    const auto original = [](const eager_block_t& block) {
        CHECK(block.nodes.size() == 1 && block.nodes[0].id == 77);
        return true;
    };
    CHECK(second.read_block(1, false, original));
    const auto replacement = files.write(header() + raw_block("OSMData", primitive(message(1, node(99)))));
    std::filesystem::rename(replacement, path);
    CHECK(first.open(path.c_str()));
    CHECK(first.read_block(1, false, [](const eager_block_t& block) {
        CHECK(block.nodes.size() == 1 && block.nodes[0].id == 99);
        return true;
    }));
    CHECK(second.read_block(1, false, original));
    first.close();
    CHECK(second.read_block(1, false, original));

    std::atomic<bool> success{true};
    std::barrier ready(4);
    std::vector<std::jthread> workers;
    for (size_t i = 0; i < 4; ++i)
        workers.emplace_back([&] {
            ready.arrive_and_wait();
            for (size_t repeat = 0; repeat < 16; ++repeat)
            {
                eager_reader_t reader;
                if (!reader.open(path.c_str()) || !reader.read_block(1, false, [](const eager_block_t& block) {
                        return block.nodes.size() == 1 && block.nodes[0].id == 99;
                    }))
                    success.store(false);
            }
        });
    for (auto& worker : workers) worker.join();
    CHECK(success.load());
}

void many_groups_test(files_t& files)
{
    const std::string deltas(512, '\x02');
    const auto dense = message(1, deltas) + message(8, deltas) + message(9, deltas) +
                       message(10, std::string(512, '\0'));
    std::string payload = table();
    for (size_t i = 0; i < 24; ++i) payload += message(2, message(2, dense));
    const auto path = files.write(header() + zlib_block(payload));
    eager_reader_t reader;
    CHECK(reader.open(path.c_str()));
    const auto complete = [](const eager_block_t& block) {
        CHECK(block.nodes.size() == 24 * 512);
        for (size_t i = 0; i < block.nodes.size(); ++i)
        {
            const auto expected = static_cast<int64_t>(i % 512 + 1);
            CHECK(block.nodes[i].id == expected && block.nodes[i].raw_latitude == expected);
            CHECK(block.nodes[i].raw_longitude == expected && block.nodes[i].tags.empty());
        }
        return true;
    };
    CHECK(reader.read_blocks(false, complete));
    CHECK(reader.read_block(1, false, complete));
    size_t batches = 0;
    CHECK(input_file(path.c_str(),
                     false,
                     [&](auto nodes) {
                         CHECK(nodes.size() == 512 && nodes.front().id == 1 && nodes.back().id == 512);
                         ++batches;
                         return true;
                     },
                     {},
                     {}));
    CHECK(batches == 24);
}
} // namespace

int main()
{
    try
    {
        input_osm::set_log_level(input_osm::LOG_LEVEL_DISABLED);
        files_t files;
        lifetime_test(files);
        access_test(files);
        failure_test(files);
        adapter_test(files);
        adapter_nonempty_test(files);
        descriptor_test(files);
        concurrent_test(files);
        large_offset_test(files);
        shared_file_test(files);
        many_groups_test(files);
        std::puts("PBF reader tests passed");
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
}
