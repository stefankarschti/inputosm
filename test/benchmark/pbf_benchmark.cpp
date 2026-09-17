#include <inputosm/inputosm.h>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sys/resource.h>

namespace
{
using benchmark_clock_t = std::chrono::steady_clock;

struct selection_t
{
    int nodes = -1, ways = -1, relations = -1;
    unsigned counts = 0;
    bool strings = false;
    bool consume_all = false;
    bool consume_ids = true;
    bool explicit_fields = false;
} selection;

struct alignas(64) totals_t
{
    uint64_t nodes = 0;
    uint64_t ways = 0;
    uint64_t relations = 0;
    uint64_t checksum = 0;
    uint64_t blocks = 0;
    uint64_t values_checksum = 0, strings = 0;
    size_t max_index = 0;

    template <class T>
    void consume(std::span<const T> entities, uint64_t& count)
    {
        count += entities.size();
        if (selection.consume_ids)
            for (const auto& entity : entities) checksum += static_cast<uint64_t>(entity.id);
    }

#if defined(INPUTOSM_BENCH_BASELINE) || defined(INPUTOSM_BENCH_READER_BASELINE)
    bool consume(const input_osm::pbf_block_t& block)
    {
        consume(block.nodes, nodes);
        consume(block.ways, ways);
        consume(block.relations, relations);
        ++blocks;
        max_index = std::max(max_index, block.index);
        return true;
    }
#else
    template <class T>
    void values(std::span<const T> values)
    {
        if (selection.consume_all)
            for (const auto value : values) values_checksum += static_cast<uint64_t>(value);
    }
    template <class Batch>
    void consume_batch(const Batch& batch, uint64_t& count)
    {
        count += batch.count;
        if (selection.consume_ids)
            for (const auto id : batch.ids) checksum += static_cast<uint64_t>(id);
        if (!selection.consume_all) return;
        for (auto tag : batch.tags.values) values_checksum += uint64_t(tag.key) * 1000003 + tag.value;
        for (auto info : batch.metadata)
            values_checksum += uint64_t(info.version) + uint64_t(info.raw_timestamp) + uint64_t(info.changeset) +
                               uint64_t(info.uid) + info.user_sid + info.present + info.visible;
    }
    bool consume(const input_osm::pbf_block_t& block)
    {
        using namespace input_osm;
        ++blocks;
        max_index = std::max(max_index, block.index());
        pbf_counts_t counted;
        if ((selection.counts & 14) == 14)
        {
            if (!block.counts(counted)) return false;
        }
        else
        {
            if ((selection.counts & 2) && !block.node_count(counted.nodes)) return false;
            if ((selection.counts & 4) && !block.way_count(counted.ways)) return false;
            if ((selection.counts & 8) && !block.relation_count(counted.relations)) return false;
        }
        if (selection.strings && !block.decode_strings([&](std::string_view value) {
                ++strings;
                if (selection.consume_all)
                    for (unsigned char byte : value) values_checksum += byte;
                return true;
            }))
            return false;
        pbf_entity_options_t options;
        if (selection.nodes >= 0)
        {
            const auto mask = selection.nodes;
            options.nodes = pbf_node_options_t{
                bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8), bool(mask & 16)};
        }
        if (selection.ways >= 0)
        {
            const auto mask = selection.ways;
            options.ways = pbf_way_options_t{bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8)};
        }
        if (selection.relations >= 0)
        {
            const auto mask = selection.relations;
            options.relations = pbf_relation_options_t{
                bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8), bool(mask & 16)};
        }
        const auto before_nodes = nodes, before_ways = ways, before_relations = relations;
        if ((options.nodes || options.ways || options.relations) &&
            !block.decode_entities(options, [&](const pbf_group_batch_t& group) {
                consume_batch(group.nodes, nodes);
                consume_batch(group.ways, ways);
                consume_batch(group.relations, relations);
                values(group.nodes.raw_latitudes);
                values(group.nodes.raw_longitudes);
                values(group.ways.node_refs.values);
                if (selection.consume_all)
                    for (auto location : group.ways.node_locations.values)
                        values_checksum += uint64_t(location.raw_latitude) + uint64_t(location.raw_longitude);
                values(group.relations.member_ids);
                values(group.relations.member_types);
                values(group.relations.member_roles);
                return true;
            }))
            return false;
        auto finish = [](bool counted_type, bool decoded_type, uint64_t before, uint64_t& total, size_t counted) {
            if (!counted_type) return;
            if (decoded_type && total - before != counted) throw std::runtime_error("Count and decode results differ");
            total = before + counted;
        };
        finish(selection.counts & 2, bool(options.nodes), before_nodes, nodes, counted.nodes);
        finish(selection.counts & 4, bool(options.ways), before_ways, ways, counted.ways);
        finish(selection.counts & 8, bool(options.relations), before_relations, relations, counted.relations);
        return true;
    }
#endif

    void add(const totals_t& other)
    {
        nodes += other.nodes;
        ways += other.ways;
        relations += other.relations;
        checksum += other.checksum;
        blocks += other.blocks;
        values_checksum += other.values_checksum;
        strings += other.strings;
        max_index = std::max(max_index, other.max_index);
    }
};

struct memory_t
{
    uint64_t rss = 0;
    uint64_t pss = 0;
    uint64_t anonymous = 0;
    uint64_t page_tables = 0;
};

memory_t memory_usage()
{
    std::ifstream input("/proc/self/smaps_rollup");
    if (!input) throw std::runtime_error("Cannot read process memory usage");
    memory_t result;
    std::string line;
    while (std::getline(input, line))
    {
        std::istringstream fields(line);
        std::string key;
        uint64_t value = 0;
        if (!(fields >> key >> value)) continue;
        if (key == "Rss:") result.rss = value;
        if (key == "Pss:") result.pss = value;
        if (key == "Pss_Anon:") result.anonymous = value;
    }
    std::ifstream status("/proc/self/status");
    while (std::getline(status, line))
    {
        if (!line.starts_with("VmPTE:")) continue;
        std::istringstream fields(line.substr(6));
        fields >> result.page_tables;
    }
    return result;
}

double seconds(timeval time)
{
    return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_usec) / 1000000.0;
}

uint64_t next_random(uint64_t& state)
{
    uint64_t value = (state += 0x9e3779b97f4a7c15ULL);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr,
                     "Usage: pbf_benchmark <file> <entities|blocks|random|random-index> <threads> <repetitions> "
                     "[metadata=0] [requests=256] [max_index=0] [--counts=MASK] [--nodes=MASK] [--ways=MASK] "
                     "[--relations=MASK] [--strings] [--consume=ids|all|count]\n");
        return EXIT_FAILURE;
    }
    try
    {
        if (const char* pid_file = std::getenv("INPUTOSM_BENCH_PID_FILE"))
        {
            std::ofstream output(pid_file);
            output << std::filesystem::read_symlink("/proc/self").string();
            if (!output) throw std::runtime_error("Cannot publish benchmark process ID");
        }
        const char* filename = argv[1];
        const std::string mode = argv[2];
        const bool random = mode == "random" || mode == "random-index";
        const size_t threads = std::stoull(argv[3]);
        const size_t repetitions = std::stoull(argv[4]);
        size_t positional[3] = {0, 256, 0};
        size_t position = 0;
        for (int arg = 5; arg < argc; ++arg)
        {
            const std::string value = argv[arg];
            if (!value.starts_with("--"))
            {
                if (position == 3) throw std::runtime_error("Too many positional arguments");
                positional[position++] = std::stoull(value);
                continue;
            }
            auto mask = [&](std::string_view prefix, int maximum) {
                const auto text = value.substr(prefix.size());
                size_t end = 0;
                int result = std::stoi(text, &end, 0);
                if (end != text.size() || result < 0 || result > maximum)
                    throw std::runtime_error("Invalid field mask");
                selection.explicit_fields = true;
                return result;
            };
            if (value.starts_with("--nodes="))
                selection.nodes = mask("--nodes=", 31);
            else if (value.starts_with("--ways="))
                selection.ways = mask("--ways=", 15);
            else if (value.starts_with("--relations="))
                selection.relations = mask("--relations=", 31);
            else if (value.starts_with("--counts="))
                selection.counts = static_cast<unsigned>(mask("--counts=", 15));
            else if (value == "--strings")
            {
                selection.strings = true;
                selection.explicit_fields = true;
            }
            else if (value == "--consume=all")
                selection.consume_all = true;
            else if (value == "--consume=count")
                selection.consume_ids = false;
            else if (value != "--consume=ids")
                throw std::runtime_error("Unknown benchmark option");
        }
        const bool metadata = positional[0] != 0;
        const size_t requests = positional[1], max_index = positional[2];
        if (!selection.explicit_fields)
        {
            selection.nodes = metadata ? 31 : 15;
            selection.ways = metadata ? 7 : 3;
            selection.relations = metadata ? 31 : 15;
        }
#if defined(INPUTOSM_BENCH_BASELINE) || defined(INPUTOSM_BENCH_READER_BASELINE)
        if (selection.explicit_fields || selection.consume_all)
            throw std::runtime_error("The eager baseline supports ID or count consumption without field selection");
#endif
        if (mode == "entities" && selection.consume_all)
            throw std::runtime_error("Use ID or count consumption for legacy input");
        if (mode == "entities" && selection.explicit_fields)
            throw std::runtime_error("The legacy API cannot select fields");
        if (threads == 0 || repetitions == 0 || (random && (requests == 0 || max_index == 0)))
            throw std::runtime_error("Invalid benchmark arguments");
        if (mode != "entities" && mode != "blocks" && !random) throw std::runtime_error("Invalid benchmark mode");
        const auto file_bytes = std::filesystem::file_size(filename);
        input_osm::set_thread_count(threads);
        if (input_osm::thread_count() != threads) throw std::runtime_error("Thread count exceeds the hardware limit");
        std::vector<size_t> indexes;
        uint64_t seed = 0x494e5055544f534dULL;
        for (size_t i = 0; i < requests && random; ++i) indexes.push_back(1 + next_random(seed) % max_index);

        std::puts(
            "mode,threads,metadata,iteration,file_bytes,requests,setup_seconds,read_seconds,total_seconds,"
            "cpu_seconds,max_rss_kib,minor_faults,major_faults,blocks,max_index,nodes,ways,relations,checksum,"
            "index_bytes,live_rss_kib,live_pss_kib,live_anon_kib,index_build_seconds,teardown_seconds,page_table_kib,"
            "node_fields,way_fields,relation_fields,count_fields,strings,values_checksum,consume");
        for (size_t iteration = 0; iteration < repetitions; ++iteration)
        {
            std::vector<totals_t> counters(threads);
            rusage before{}, after{};
            getrusage(RUSAGE_SELF, &before);
            const auto start = benchmark_clock_t::now();
            auto ready = start;
            auto work_finish = start;
            bool ok = true;
            uint64_t index_bytes = 0;
            double measurement_seconds = 0;
            double index_build_seconds = 0;
            memory_t memory;
            auto measure_memory = [&] {
                work_finish = benchmark_clock_t::now();
                memory = memory_usage();
                measurement_seconds += std::chrono::duration<double>(benchmark_clock_t::now() - work_finish).count();
            };
            if (mode == "entities")
            {
                ok = input_osm::input_file(
                    filename,
                    metadata,
                    [&](auto nodes) {
                        auto& count = counters[input_osm::thread_index];
                        count.consume(nodes, count.nodes);
                        return true;
                    },
                    [&](auto ways) {
                        auto& count = counters[input_osm::thread_index];
                        count.consume(ways, count.ways);
                        return true;
                    },
                    [&](auto relations) {
                        auto& count = counters[input_osm::thread_index];
                        count.consume(relations, count.relations);
                        return true;
                    });
                measure_memory();
            }
            else if (mode == "blocks")
            {
                const auto handler = [&](const input_osm::pbf_block_t& block) {
                    return counters[input_osm::thread_index].consume(block);
                };
#ifdef INPUTOSM_BENCH_BASELINE
                ok = input_osm::input_pbf_blocks(filename, metadata, handler);
                measure_memory();
#else
                input_osm::pbf_reader_t reader;
                reader.set_thread_count(threads);
                ok = reader.open(filename);
                ready = benchmark_clock_t::now();
#ifdef INPUTOSM_BENCH_READER_BASELINE
                if (ok) ok = reader.read_blocks(metadata, handler);
#else
                if (ok) ok = reader.read_blocks(handler);
#endif
                measure_memory();
#endif
            }
            else
            {
#ifdef INPUTOSM_BENCH_BASELINE
                throw std::runtime_error("The original API has no indexed reader");
#else
                std::vector<input_osm::pbf_reader_t> readers(threads);
                for (auto& reader : readers)
                    if (!reader.open(filename)) throw std::runtime_error("Cannot open benchmark input");
                std::atomic<bool> success{true};
                std::barrier synchronization(static_cast<std::ptrdiff_t>(threads + 1));
                const auto index_start = benchmark_clock_t::now();
                std::vector<std::jthread> workers;
                workers.reserve(threads);
                for (size_t worker = 0; worker < threads; ++worker)
                    workers.emplace_back([&, worker] {
                        if (mode == "random-index" && !readers[worker].build_index()) success.store(false);
                        synchronization.arrive_and_wait();
                        synchronization.arrive_and_wait();
                        for (size_t request = worker; request < indexes.size(); request += threads)
                        {
                            const size_t index = indexes[request];
                            const auto handler = [&](const auto& block) {
                                return counters[worker].consume(block);
                            };
#ifdef INPUTOSM_BENCH_READER_BASELINE
                            if (!readers[worker].read_block(index, metadata, handler))
#else
                            if (!readers[worker].read_block(index, handler))
#endif
                                success.store(false, std::memory_order_relaxed);
                        }
                    });
                synchronization.arrive_and_wait();
                ready = benchmark_clock_t::now();
                if (mode == "random-index")
                    index_build_seconds = std::chrono::duration<double>(ready - index_start).count();
                synchronization.arrive_and_wait();
                for (auto& worker : workers) worker.join();
                ok = success.load(std::memory_order_relaxed);
                for (const auto& reader : readers) index_bytes += reader.index_memory_bytes();
                measure_memory();
#endif
            }
            const auto finish = benchmark_clock_t::now();
            getrusage(RUSAGE_SELF, &after);
            if (!ok) throw std::runtime_error("Benchmark read failed");
            totals_t total;
            for (const auto& count : counters) total.add(count);
            std::printf(
                "%s,%zu,%u,%zu,%llu,%zu,%.9f,%.9f,%.9f,%.6f,%ld,%ld,%ld,%llu,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%"
                "llu,%.9f,%.9f,%llu,%d,%d,%d,%u,%llu,%llu,%s\n",
                mode.c_str(),
                threads,
                metadata,
                iteration,
                static_cast<unsigned long long>(file_bytes),
                random ? requests : 0,
                std::chrono::duration<double>(ready - start).count(),
                std::chrono::duration<double>(work_finish - ready).count(),
                std::chrono::duration<double>(finish - start).count() - measurement_seconds,
                seconds(after.ru_utime) + seconds(after.ru_stime) - seconds(before.ru_utime) - seconds(before.ru_stime),
                after.ru_maxrss,
                after.ru_minflt - before.ru_minflt,
                after.ru_majflt - before.ru_majflt,
                static_cast<unsigned long long>(total.blocks),
                total.max_index,
                static_cast<unsigned long long>(total.nodes),
                static_cast<unsigned long long>(total.ways),
                static_cast<unsigned long long>(total.relations),
                static_cast<unsigned long long>(total.checksum),
                static_cast<unsigned long long>(index_bytes),
                static_cast<unsigned long long>(memory.rss),
                static_cast<unsigned long long>(memory.pss),
                static_cast<unsigned long long>(memory.anonymous),
                index_build_seconds,
                std::chrono::duration<double>(finish - work_finish).count() - measurement_seconds,
                static_cast<unsigned long long>(memory.page_tables),
                selection.nodes,
                selection.ways,
                selection.relations,
                selection.counts,
                static_cast<unsigned long long>(total.strings),
                static_cast<unsigned long long>(total.values_checksum),
                selection.consume_all   ? "all"
                : selection.consume_ids ? "ids"
                                        : "count");
            std::fflush(stdout);
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
