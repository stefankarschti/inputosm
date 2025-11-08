# inputosm

High-performance, multi-threaded, zero shared-state reader for OpenStreetMap datasets supporting PBF, XML (*.osm) and OSC (change) files. Designed for bulk analytical ingestion where raw throughput and low per-entity overhead matter.

> Fast planet-scale parsing with simple callback-based streaming APIs.

## Contents

1. Features
2. Quick Start
3. Build & Install
4. Using Conan (optional)
5. CMake Options
6. API Overview
7. Usage Examples
8. Logging & Diagnostics
9. Performance & Benchmarks
10. Architecture Notes
11. FAQ
12. Contributing
13. License

---

## 1. Features

* Multi-threaded decoding of OSM PBF and XML
* Lock-free usage pattern – each worker thread invokes user callbacks independently
* Batch (span) delivery of homogeneous entity groups (nodes, ways, relations) for cache-friendly processing
* Optional metadata decoding (version, timestamp, changeset)
* Pluggable logging callback (thread-safe user callback)
* Minimal dependencies: Zlib + Expat only
* Modern C++20, header-first public API with plain structs (POD) <= 64 bytes each for node/way/relation
* Simple integration: one function `input_file()` + a few configuration utilities

## 2. Quick Start

```cpp
#include <inputosm/inputosm.h>
#include <cstdint>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: demo <file.osm.pbf> [meta]\n"; return 1; }
    const char* file = argv[1];
    const bool read_meta = (argc >= 3);
    input_osm::set_max_thread_count(); // use all hardware threads

    uint64_t node_total = 0, way_total = 0, rel_total = 0;

    bool ok = input_osm::input_file(
        file,
        read_meta,
        [&node_total](input_osm::span_t<input_osm::node_t> nodes){ node_total += nodes.size(); return true; },
        [&way_total](input_osm::span_t<input_osm::way_t> ways){ way_total += ways.size(); return true; },
        [&rel_total](input_osm::span_t<input_osm::relation_t> rels){ rel_total += rels.size(); return true; }
    );

    if(!ok) { std::cerr << "Parse failed\n"; return 2; }
    std::cout << "nodes=" << node_total << " ways=" << way_total << " relations=" << rel_total << "\n";
}
```

Build & run (Linux):

```bash
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
./build/test/integration/count_all path/to/planet.osm.pbf 1
```

## 3. Build & Install

Requirements:

* CMake >= 3.16
* C++20 compiler (g++ ≥ 9, clang ≥ 12, Apple clang ≥ 13, MSVC supported with /std:c++20)
* Dependencies: Expat, Zlib

Standard build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target inputosm --parallel $(nproc)
```

Install (headers + library + CMake package + pkg-config file):

```bash
cmake --build build --target install
```

Using Ninja:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target install
```

Find with CMake in downstream project:

```cmake
find_package(inputosm REQUIRED)
target_link_libraries(mytool PRIVATE inputosm::inputosm)
```

pkg-config:

```bash
pkg-config --cflags --libs inputosm
```

## 4. Using Conan (optional)

You can let Conan resolve Expat & Zlib:

```bash
cmake -S conan -B build/conan-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/conan-release --target install --parallel $(nproc)
```

Suggested VSCode settings snippet:

```json
{
  "cmake.buildDirectory": "${workspaceFolder}/build/${buildKit}-${buildType}",
  "cmake.installPrefix": "${workspaceFolder}/install",
  "cmake.sourceDirectory": "${workspaceFolder}/conan"
}
```

## 5. CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `INPUTOSM_INTEGRATION_TESTS` | ON | Build integration examples / benchmarks |
| `WARNINGS_AS_ERRORS` | ON | Treat warnings as errors (`-Werror`) |
| `ENABLE_CLANG_TIDY` | ON | Enforce clang-tidy if available (fails if not found) |

Disable an option, e.g.:

```bash
cmake -S . -B build -DINPUTOSM_INTEGRATION_TESTS=OFF -DENABLE_CLANG_TIDY=OFF
```

## 6. API Overview

Namespace: `input_osm`.

Core structs (POD, <= 64 bytes):

* `node_t { int64_t id; int64_t raw_latitude; int64_t raw_longitude; span_t<tag_t> tags; int32_t version; int32_t timestamp; int32_t changeset; }`
* `way_t { int64_t id; span_t<int64_t> node_refs; span_t<tag_t> tags; int32_t version; int32_t timestamp; int32_t changeset; }`
* `relation_t { int64_t id; span_t<relation_member_t> members; span_t<tag_t> tags; int32_t version; int32_t timestamp; int32_t changeset; }`
* `tag_t { const char* key; const char* value; }` (string views valid only during callback)
* `relation_member_t { uint8_t type; int64_t id; const char* role; }` (`type`: 0=node,1=way,2=relation)

Execution control:

* `bool input_file(const char* path, bool decode_metadata, node_handler, way_handler, relation_handler)`
  * Each handler: `std::function<bool(span_t<T>)>`; return `false` to abort early.
* `void set_verbose(bool)` – extra diagnostic output (stderr)
* `void set_thread_count(size_t)` / `void set_max_thread_count()` / `size_t thread_count()`
* Thread-local indices exposed: `thread_local size_t thread_index; thread_local size_t block_index;`

Logging:

* `enum log_level_t { TRACE, INFO, ERROR, DISABLED }`
* `void set_log_level(log_level_t)` (not thread-safe; set at start)
* `bool set_log_callback(log_callback_t)` where `log_callback_t` is `void(*)(log_level_t,const char*)`

Aux utilities (time): `now_ms()`, `now_us()`, `str_to_timestamp()`, `timestamp_to_str()`, etc.

### Lifetime & Ownership
Pointers inside `tag_t`, `relation_member_t::role` remain valid only for the duration of the callback invocation supplying their containing span. Copy what you need if retaining after return.

### Concurrency Model
Entities are partitioned and processed in parallel. Each user handler may be invoked concurrently on different threads. Avoid shared mutable state or protect it appropriately. Use the provided `thread_index` to index thread-local arrays/vectors (see examples).

## 7. Usage Examples

### 7.1 Counting Entities (from `count_all.cpp`)

```cpp
std::vector<input_osm::Counter<uint64_t>> counters(3 * input_osm::thread_count());
auto nodes = std::span{counters.data(), input_osm::thread_count()};
auto ways  = std::span{counters.data()+input_osm::thread_count(), input_osm::thread_count()};
auto rels  = std::span{counters.data()+2*input_osm::thread_count(), input_osm::thread_count()};

input_osm::input_file(
  file, read_meta,
  [&nodes](auto batch){ nodes[input_osm::thread_index] += batch.size(); return true; },
  [&ways](auto batch){ ways[input_osm::thread_index] += batch.size(); return true; },
  [&rels](auto batch){ rels[input_osm::thread_index] += batch.size(); return true; }
);
```

### 7.2 Custom Logging (from `custom_log.cpp`)

```cpp
const auto logWithTime = [](input_osm::log_level_t level, const char* msg){ /* format & print */ };
input_osm::set_log_level(input_osm::LOG_LEVEL_TRACE);
input_osm::set_log_callback(logWithTime);
```

### 7.3 Gathering Statistics (excerpt from `statistics.cpp`)

```cpp
    std::vector<input_osm::u64_64B> node_count(input_osm::thread_count(), 0);
    std::vector<input_osm::u64_64B> way_count(input_osm::thread_count(), 0);
    std::vector<input_osm::u64_64B> relation_count(input_osm::thread_count(), 0);

    std::vector<input_osm::u64_64B> max_node_count(input_osm::thread_count(), 0);
    std::vector<input_osm::u64_64B> max_node_tag_count(input_osm::thread_count(), 0);

    std::vector<input_osm::u64_64B> max_way_count(input_osm::thread_count(), 0);
    std::vector<input_osm::u64_64B> max_way_tag_count(input_osm::thread_count(), 0);
    std::vector<input_osm::u64_64B> max_way_node_count(input_osm::thread_count(), 0);

    std::vector<input_osm::u64_64B> max_relation_count(input_osm::thread_count(), 0);
    std::vector<input_osm::u64_64B> max_relation_tag_count(input_osm::thread_count(), 0);
    std::vector<input_osm::u64_64B> max_relation_member_count(input_osm::thread_count(), 0);

    std::vector<input_osm::i32_64B> node_timestamp(input_osm::thread_count(), 0);
    std::vector<input_osm::i32_64B> way_timestamp(input_osm::thread_count(), 0);
    std::vector<input_osm::i32_64B> relation_timestamp(input_osm::thread_count(), 0);

    std::vector<input_osm::u64_64B> block_index(input_osm::thread_count(), 0);

    std::vector<input_osm::u64_64B> node_with_tags_count(input_osm::thread_count(), 0);
    std::vector<input_osm::u64_64B> ways_with_tags_count(input_osm::thread_count(), 0);
    std::vector<input_osm::u64_64B> relations_with_tags_count(input_osm::thread_count(), 0);

    std::vector<input_osm::i64_64B> max_node_id(input_osm::thread_count(), 0);
    std::vector<input_osm::i64_64B> max_way_id(input_osm::thread_count(), 0);
    std::vector<input_osm::i64_64B> max_relation_id(input_osm::thread_count(), 0);

    if (!input_osm::input_file(
            path,
            read_metadata,
            [&node_count,
             &max_node_count,
             &max_node_tag_count,
             &node_timestamp,
             &block_index,
             &node_with_tags_count,
             &max_node_id](input_osm::span_t<input_osm::node_t> node_list) noexcept -> bool {
                auto cnt = node_list.size();
                node_count[input_osm::thread_index] += cnt;
                if (cnt > max_node_count[input_osm::thread_index]) max_node_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for (auto &n : node_list) cnt += n.tags.size();
                if (cnt > max_node_tag_count[input_osm::thread_index])
                    max_node_tag_count[input_osm::thread_index] = cnt;
                for (auto &n : node_list)
                    node_timestamp[input_osm::thread_index] = std::max<int32_t>(node_timestamp[input_osm::thread_index],
                                                                                n.timestamp);
                block_index[input_osm::thread_index] = std::max<uint64_t>(block_index[input_osm::thread_index],
                                                                          input_osm::block_index);
                for (auto &n : node_list)
                    if (!n.tags.empty()) node_with_tags_count[input_osm::thread_index]++;
                for (auto &n : node_list)
                    max_node_id[input_osm::thread_index] = std::max<int64_t>(max_node_id[input_osm::thread_index],
                                                                             n.id);
                return true;
            },
            [&way_count,
             &max_way_count,
             &max_way_tag_count,
             &max_way_node_count,
             &way_timestamp,
             &block_index,
             &ways_with_tags_count,
             &max_way_id](input_osm::span_t<input_osm::way_t> way_list) noexcept -> bool {
                auto cnt = way_list.size();
                way_count[input_osm::thread_index] += cnt;
                if (cnt > max_way_count[input_osm::thread_index]) max_way_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for (auto &w : way_list) cnt += w.tags.size();
                if (cnt > max_way_tag_count[input_osm::thread_index]) max_way_tag_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for (auto &w : way_list) cnt += w.node_refs.size();
                if (cnt > max_way_node_count[input_osm::thread_index])
                    max_way_node_count[input_osm::thread_index] = cnt;
                for (auto &w : way_list)
                    way_timestamp[input_osm::thread_index] = std::max<int32_t>(way_timestamp[input_osm::thread_index],
                                                                               w.timestamp);
                block_index[input_osm::thread_index] = std::max<uint64_t>(block_index[input_osm::thread_index],
                                                                          input_osm::block_index);
                for (auto &w : way_list)
                    if (!w.tags.empty()) ways_with_tags_count[input_osm::thread_index]++;
                for (auto &w : way_list)
                    max_way_id[input_osm::thread_index] = std::max<int64_t>(max_way_id[input_osm::thread_index], w.id);
                return true;
            },
            [&relation_count,
             &max_relation_count,
             &max_relation_tag_count,
             &max_relation_member_count,
             &relation_timestamp,
             &block_index,
             &relations_with_tags_count,
             &max_relation_id](input_osm::span_t<input_osm::relation_t> relation_list) noexcept -> bool {
                auto cnt = relation_list.size();
                relation_count[input_osm::thread_index] += cnt;
                if (cnt > max_relation_count[input_osm::thread_index])
                    max_relation_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for (auto &r : relation_list) cnt += r.tags.size();
                if (cnt > max_relation_tag_count[input_osm::thread_index])
                    max_relation_tag_count[input_osm::thread_index] = cnt;
                cnt = 0;
                for (auto &r : relation_list) cnt += r.members.size();
                if (cnt > max_relation_member_count[input_osm::thread_index])
                    max_relation_member_count[input_osm::thread_index] = cnt;
                for (auto &r : relation_list)
                    relation_timestamp[input_osm::thread_index] = std::max<int32_t>(
                        relation_timestamp[input_osm::thread_index], r.timestamp);
                block_index[input_osm::thread_index] = std::max<uint64_t>(block_index[input_osm::thread_index],
                                                                          input_osm::block_index);
                for (auto &r : relation_list)
                    if (!r.tags.empty()) relations_with_tags_count[input_osm::thread_index]++;
                for (auto &r : relation_list)
                    max_relation_id[input_osm::thread_index] = std::max<int64_t>(
                        max_relation_id[input_osm::thread_index], r.id);
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
    std::cout << "max node tags per block: " << *std::max_element(max_node_tag_count.begin(), max_node_tag_count.end())
              << "\n";

    std::cout << "max ways per block: " << *std::max_element(max_way_count.begin(), max_way_count.end()) << "\n";
    std::cout << "max way tags per block: " << *std::max_element(max_way_tag_count.begin(), max_way_tag_count.end())
              << "\n";
    std::cout << "max way nodes per block: " << *std::max_element(max_way_node_count.begin(), max_way_node_count.end())
              << "\n";

    std::cout << "max relations per block: " << *std::max_element(max_relation_count.begin(), max_relation_count.end())
              << "\n";
    std::cout << "max relation tags per block: "
              << *std::max_element(max_relation_tag_count.begin(), max_relation_tag_count.end()) << "\n";
    std::cout << "max relation members per block: "
              << *std::max_element(max_relation_member_count.begin(), max_relation_member_count.end()) << "\n";

    auto timestamp_to_str = [](const time_t in_time_t) -> std::string {
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&in_time_t), "%F %T %Z");
        return ss.str();
    };

    std::cout << "max node timestamp: "
              << timestamp_to_str(*std::max_element(node_timestamp.begin(), node_timestamp.end())) << std::endl;
    std::cout << "max way timestamp: "
              << timestamp_to_str(*std::max_element(way_timestamp.begin(), way_timestamp.end())) << std::endl;
    std::cout << "max relation timestamp: "
              << timestamp_to_str(*std::max_element(relation_timestamp.begin(), relation_timestamp.end())) << std::endl;

    std::cout << "max file block index: " << *std::max_element(block_index.begin(), block_index.end()) << std::endl;

    std::cout << "nodes with tags: " << std::accumulate(node_with_tags_count.begin(), node_with_tags_count.end(), 0LLU)
              << "\n";
    std::cout << "ways with tags: " << std::accumulate(ways_with_tags_count.begin(), ways_with_tags_count.end(), 0LLU)
              << "\n";
    std::cout << "relations with tags: "
              << std::accumulate(relations_with_tags_count.begin(), relations_with_tags_count.end(), 0LLU) << "\n";

    std::cout << "max node id: " << *std::max_element(max_node_id.begin(), max_node_id.end()) << "\n";
    std::cout << "max way id: " << *std::max_element(max_way_id.begin(), max_way_id.end()) << "\n";
    std::cout << "max relation id: " << *std::max_element(max_relation_id.begin(), max_relation_id.end()) << "\n";
```

Shows advanced per-block maxima & timestamp extraction using thread-local accumulation arrays and the provided `block_index`.

## 8. Logging & Diagnostics

Enable verbose internal prints:

```cpp
input_osm::set_verbose(true);
```

Install a callback to receive asynchronous multi-thread log messages. The callback MUST be thread-safe and fast. Consider queueing messages if heavy formatting is required.

To disable logging entirely: `set_log_level(LOG_LEVEL_DISABLED);`

## 9. Performance & Benchmarks

Planet (2022-09-05) on dual Xeon E5-2699 (72 threads total):

```
real    0m28.215s
user    30m23.402s
sys     0m18.354s
```

Extract from benchmark output:

```
nodes: 7,894,460,004
ways: 884,986,817
relations: 10,199,553
max nodes per block: 16,000
max way nodes per block: 833,428
```

These figures demonstrate high parallel efficiency (user time >> wall clock). Throughput is primarily bounded by I/O and decompression.

### Tips for Maximum Throughput
* Build Release with full optimization (`-O3` typically via CMake Release)
* Use fast storage (NVMe / RAM disk) – decompression and parsing are CPU-heavy but still benefit from prefetching
* Pin process / set CPU affinity if running alongside other heavy workloads
* Avoid heavy work in callbacks; batch & defer where possible

## 10. Architecture Notes

High-level pipeline:

1. File block enumeration & decompression (PBF) or streaming parsing (XML)
2. Work queue of decompressed blocks distributed across worker threads
3. Per-thread decoding into transient POD batches (vectors/spans)
4. User callbacks invoked with contiguous spans – no per-entity dynamic allocation inside hot path

Design goals: minimize synchronization, keep data structures small, and expose raw integers for ids and fixed-point lat/lon (`raw_latitude`, `raw_longitude` scaled as in OSM PBF: multiply by 1e-7 to get degrees if standard scaling was used – confirm in your own conversion layer).

## 11. FAQ

Q: How do I stop early?  
A: Return `false` from any handler; parsing stops and `input_file` returns `false`.

Q: Are tag strings null-terminated?  
A: Yes. They remain valid only in the scope of the callback.

Q: How do I convert raw lat/lon?  
A: Typically `double lat = raw_latitude * 1e-7;` and same for longitude (depending on source scaling).

Q: Does it support diff (OSC) mode?  
A: Modes are enumerated by `mode_t` (bulk/create/modify/destroy). `osc_mode` indicates the current change set context when parsing an OSC file.

Q: What about relations with very many members?  
A: Batches are sized to keep struct size small; extremely large relations are still delivered within the span; copy or stream as needed before returning.

## 12. Contributing

We welcome:

* Issues with reproducible cases (include dataset snippet if possible)
* Pull requests (keep changes focused, add integration example if API-visible)
* Performance profiling reports
* Additional benchmarks or formats

Guidelines:

* Run integration examples: `-DINPUTOSM_INTEGRATION_TESTS=ON`
* Keep public structs stable; propose changes via issue first
* Add tests for new logic
* Follow existing style; enable `ENABLE_CLANG_TIDY` locally when possible

## 13. License

Apache License 2.0. See `LICENSE` file.

---

Feel free to open an issue for clarifications or feature requests.

