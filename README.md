# inputosm

inputosm is a C++20 library that reads OpenStreetMap (OSM) data.
It reads binary PBF files, XML `.osm` files, and XML `.osc` change files.
The library calls your functions with groups of nodes, ways, or relations.
A callback is a function that your application gives to the library.

The PBF reader can use multiple worker threads.
The XML reader uses the thread that calls `input_file()`.

## Contents

1. Features
2. Start
3. Build and install
4. Conan usage
5. CMake options
6. API description
7. Examples
8. Logs and diagnostics
9. Performance and benchmarks
10. Architecture
11. Questions and answers
12. Contribute
13. License

## 1. Features

- Read PBF data with multiple threads.
- Read OSM data and OSC changes in XML format.
- Get a span of entities in each callback. A span gives access to a sequence of objects in adjacent memory locations.
- Use different callbacks for nodes, ways, and relations.
- Select metadata, such as versions, timestamps, and changesets, for PBF decoding.
- Set a callback for log messages.
- Use Expat, libdeflate, and fmt as the library dependencies.
- Use node, way, and relation structures that each occupy a maximum of 64 bytes.

## 2. Start

This example counts entities with one thread.
Section 7.1 shows counters for multiple threads.

```cpp
#include <inputosm/inputosm.h>
#include <cstdint>
#include <fmt/format.h>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { fmt::print(stderr, "Usage: demo <file.osm.pbf> [meta]\n"); return 1; }
    const char* file = argv[1];
    const bool read_meta = (argc >= 3);
    input_osm::set_thread_count(1); // Use one thread for these shared counters.

    uint64_t node_total = 0, way_total = 0, rel_total = 0;

    bool ok = input_osm::input_file(
        file,
        read_meta,
        [&node_total](std::span<const input_osm::node_t> nodes){ node_total += nodes.size(); return true; },
        [&way_total](std::span<const input_osm::way_t> ways){ way_total += ways.size(); return true; },
        [&rel_total](std::span<const input_osm::relation_t> rels){ rel_total += rels.size(); return true; }
    );

    if(!ok) { fmt::print(stderr, "Parse failed\n"); return 2; }
    fmt::print("nodes={} ways={} relations={}\n",
               fmt::group_digits(node_total),
               fmt::group_digits(way_total),
               fmt::group_digits(rel_total));
}
```

To build and use the supplied `count_all` example on Linux:

1. Configure the build.

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   ```

2. Build the project.

   ```bash
   cmake --build build --parallel $(nproc)
   ```

3. Start `count_all` with the path to an OSM file.

   ```bash
   ./build/test/integration/count_all path/to/planet.osm.pbf 1
   ```

The last argument selects metadata decoding.
The supplied `count_all` example uses multiple threads and a different counter for each thread.

## 3. Build and install

### Requirements

- CMake version 3.16 or a subsequent version
- C and C++20 compilers
- An operating system with the POSIX interfaces that the source files use
- Expat
- clang-tidy, unless `ENABLE_CLANG_TIDY` is `OFF`

CMake FetchContent downloads libdeflate 1.26 and fmt 12.2.0.
It checks the SHA-256 checksum of each archive.
The first configuration requires network access.
For an offline build, set `FETCHCONTENT_SOURCE_DIR_LIBDEFLATE` to a local libdeflate 1.26 source directory.
For an offline build, set `FETCHCONTENT_SOURCE_DIR_FMT` to a local fmt 12.2.0 source directory.
The build uses the compiled `fmt::fmt` target.
libdeflate uses the [MIT license](https://github.com/ebiggers/libdeflate/blob/v1.26/COPYING), which permits use with the inputosm Apache-2.0 license.
The reader keeps one decompressor and one reusable output buffer per worker.
It checks the zlib-format checksum, the complete compressed input length, and the exact output size.

### Build the library

1. Configure a Release build.

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   ```

2. Build the library.

   ```bash
   cmake --build build --target inputosm --parallel
   ```

3. Install the package if necessary.

   ```bash
   cmake --build build --target install
   ```

The package contains headers, the library, CMake package files, and a pkg-config file.
It also installs the fetched libdeflate static library, package files, header, and license notice.
The installation also includes fmt and its package files.

### Build with Ninja

1. Select the Ninja generator.

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   ```

2. Install the package.

   ```bash
   cmake --build build --target install
   ```

### Use the library in a different project

Add these commands to the CMake project:

```cmake
find_package(inputosm REQUIRED)
target_link_libraries(mytool PRIVATE inputosm::inputosm)
```

If your program calls fmt directly, also link its target to `fmt::fmt`.

To get the compiler and linker options with pkg-config, use this command:

```bash
pkg-config --cflags --libs inputosm
```

For a static inputosm library, add `--static` to include the private dependencies.

## 4. Conan usages

Conan can get Expat.
CMake FetchContent supplies libdeflate and fmt.

1. Configure the project through the Conan directory.

   ```bash
   cmake -S conan -B build/conan-release -DCMAKE_BUILD_TYPE=Release
   ```

2. Install the package.

   ```bash
   cmake --build build/conan-release --target install --parallel
   ```

To use this directory in Visual Studio Code, add these settings:

```json
{
  "cmake.buildDirectory": "${workspaceFolder}/build/${buildKit}-${buildType}",
  "cmake.installPrefix": "${workspaceFolder}/install",
  "cmake.sourceDirectory": "${workspaceFolder}/conan"
}
```

## 5. CMake options

| Option | Default | Description |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | Build the unit tests. |
| `INPUTOSM_INTEGRATION_TESTS` | `ON` | Build the integration examples and benchmarks. |
| `WARNINGS_AS_ERRORS` | `ON` | Stop the build if the compiler gives warnings. |
| `ENABLE_CLANG_TIDY` | `ON` | Use clang-tidy. Configuration fails if CMake cannot find it. |

To disable the integration examples and clang-tidy, use this command:

```bash
cmake -S . -B build -DINPUTOSM_INTEGRATION_TESTS=OFF -DENABLE_CLANG_TIDY=OFF
```

## 6. API description

The public application programming interface (API) uses the `input_osm` namespace.
The declarations are in `include/inputosm/inputosm.h`.

### Data structures

The `node_t`, `way_t`, and `relation_t` structures each occupy a maximum of 64 bytes.
Their spans refer to memory for tags, node references, and relation members.
This memory is not part of the structures.

- `node_t { int64_t id; int64_t raw_latitude; int64_t raw_longitude; std::span<const tag_t> tags; int32_t version; int32_t timestamp; int32_t changeset; }`
- `way_t { int64_t id; std::span<const int64_t> node_refs; std::span<const tag_t> tags; int32_t version; int32_t timestamp; int32_t changeset; }`
- `relation_t { int64_t id; std::span<const relation_member_t> members; std::span<const tag_t> tags; int32_t version; int32_t timestamp; int32_t changeset; }`
- `tag_t { std::string_view key; std::string_view value; }`
- `relation_member_t { uint8_t type; int64_t id; std::string_view role; }`

A relation member has type `0` for a node, `1` for a way, or `2` for a relation.

### Read a file

Use `input_file()` with a path, a metadata option, and three handlers.
A handler is a callback for one entity type.
Each handler has the type `std::function<bool(std::span<const T>)>` for its entity type `T`.
A handler returns `true` to continue.
Refer to section 11 for the limits on cancellation.

The `decode_metadata` option controls PBF metadata decoding.
This option does not change how the XML reader reads metadata attributes.

### Read complete PBF blocks

Use `pbf_reader_t::read_blocks()` for one callback per `OSMData` block.
The callback receives all nodes, ways, relations, and string table entries from that block.
The Boolean result is `true` on completion and `false` on cancellation or error.
The reader opens PBF content without a filename extension check.

```cpp
input_osm::pbf_reader_t reader;
reader.set_max_thread_count();
const bool ok = reader.open("map.osm.pbf") && reader.read_blocks(
    false,
    [](const input_osm::pbf_block_t& block) {
        for (const auto& way : block.ways)
            for (const auto& tag : way.tags)
                if (tag.key == "route" && tag.value == "ferry")
                    fmt::print("{}\n", way.id);
        return true;
    });
```

The `count_blocks` integration example counts blocks and entities.
`block.index` is the file block ordinal, including the initial header at index zero.
`block.file_offset` identifies the four-byte length field in the input file.
The block also exposes `granularity`, `lat_offset`, `lon_offset`, and `date_granularity`.

All PBF input methods use string views directly into raw or decompressed block bytes.
A complete block callback also receives `std::span<const std::string_view> string_table`.
This span preserves table indexes, duplicates, empty strings, and unused entries.
A missing or invalid table causes an error before the block callback.
The reader supports raw and Zlib Blobs, ordinary nodes, and dense nodes.
It rejects unsupported required features, including historical visibility.

With one thread, callbacks run in file order in the calling thread.
With multiple threads, callbacks can overlap and finish in a different order.
The reader keeps its file mapping until `close()` or destruction.
Each sequential call starts at the file start.
Keep the file contents unchanged while the reader is open.
Refer to the [reader design](docs/pbf-random-access-proposal.md) for the complete contract.
The [performance report](docs/pbf-reader-performance.md) compares sequential reads, random scans, and offset tables.

### Read a block by index

Use `read_block()` with a file block index from an earlier block callback.
The method calls the handler once in the calling thread.
Header blocks, unknown block types, and unavailable indexes cause `false` without a callback.

```cpp
input_osm::pbf_reader_t reader;
if (!reader.open("map.osm.pbf")) return 1;
if (!reader.build_index()) return 1;
const bool ok = reader.read_block(100, false, [](const input_osm::pbf_block_t& block) {
    fmt::print("Block {} has {} nodes\n", block.index, block.nodes.size());
    return true;
});
```

`build_index()` is optional.
Without it, each indexed read scans earlier block headers and skips their payloads.
With it, the reader uses an in-memory offset table to find the requested block directly.
The table uses eight bytes per file block, plus vector capacity overhead.
`index_memory_bytes()` reports the allocated vector storage in bytes.
`has_index()` reports whether the table is available.

Index construction checks framing through the file end without decoding data payloads.
An invalid later frame can make index construction fail even when an earlier block supports a scan-based read.
A failed index construction leaves the reader open without a partial table.
An existing index remains available after a payload decoding error.
Sequential reads always use a bounded queue and do not require an index.

Use separate reader objects for simultaneous indexed requests.
Each reader keeps its own offset table and decoder storage.
The [benchmark guide](test/benchmark/README.md) describes the scan and index comparison.

### Migrate to version 0.3.0

This version replaces the free `input_pbf_blocks()` function with `pbf_reader_t::read_blocks()`.
Rebuild dependent applications with the new headers and library.

1. Construct a reader.
2. Configure its member thread setting.
3. Open the input file.
4. Call `read_blocks()` with the metadata setting and block handler.

Each reader starts with one thread.
Global thread settings apply to `input_file()` and do not configure independent reader objects.
Reader callbacks use their block argument and thread-local context instead of the global `file_type` and `osc_mode` variables.

PBF `input_file()` uses a temporary reader and preserves primitive-group entity batches.
The reader uses one decoder implementation for entity and block callbacks.
Public block callbacks receive complete blocks.
A handler failure stops subsequent entity calls for that group.
Other callbacks that already started can finish during cancellation.
Complete block callbacks require more decoder memory than primitive-group entity callbacks.
Readers of the same unchanged file share its mapping and keep separate decoders and indexes.
XML and OSC callback rules remain unchanged.

### Migrate to version 0.2.0

This version changes source and binary interfaces.
Rebuild dependent applications with the new headers and library.

- Replace the custom span with `std::span<const T>` in entity handlers.
- Include `<span>` instead of the removed `inputosm/span.h` header.
- Replace C-string comparisons with string view comparisons.
- Use `.size()` instead of `strlen()` for tag values and relation roles.
- Copy a view into `std::string` when an application needs owned text.
- Preserve empty tag values when removing old pointer checks.

`input_file()` retains its three entity handlers and Boolean return value.
PBF stop requests and worker failures now reach the caller as `false`.
XML and OSC use the same public entity types with views into temporary owned strings.

### Configure threads and logs

| Function or variable | Description |
| --- | --- |
| `set_thread_count(size_t)` | Set the number of PBF threads. The hardware thread count is the maximum. |
| `set_max_thread_count()` | Select the hardware thread count for PBF input. |
| `thread_count()` | Get the configured thread count. The minimum result is 1. |
| `pbf_reader_t::set_thread_count(size_t)` | Set the sequential worker count for this reader. Zero selects one thread. |
| `pbf_reader_t::set_max_thread_count()` | Select the hardware thread count for this reader. |
| `pbf_reader_t::thread_count()` | Get this reader's sequential worker count. Indexed reads use the calling thread. |
| `thread_index` | Thread-local index for this worker. |
| `block_index` | Thread-local index for this PBF block. |
| `set_verbose(bool)` | Set the verbose flag. The reader does not use this flag. |
| `set_log_level(log_level_t)` | Set the minimum log level. This function is not thread-safe. |
| `set_log_callback(log_callback_t)` | Set the log callback. A null callback causes the function to return `false`. |

The log levels are `LOG_LEVEL_TRACE`, `LOG_LEVEL_INFO`, `LOG_LEVEL_ERROR`, and `LOG_LEVEL_DISABLED`.
The callback type is `void (*)(log_level_t, const char*)`.

### Data lifetime

Use the spans and their data only during the callback that receives them.
This limit includes tag strings and relation roles.
To use data after the callback returns, copy the data into memory that your application owns.
A copy of a structure alone does not copy the data to which its spans and string views refer.

### Concurrent access

PBF workers can call the same handler at the same time.
Use a different counter for each thread.
If threads share data, use synchronization to prevent concurrent changes.
Use `thread_index` to select the array or vector entry for this thread.

Separate reader objects can run concurrently with independent thread settings and handlers.
Use one public operation at a time on each reader.
Do not use `thread_index` as a unique worker identifier across different readers.
The reader restores thread-local context after each callback.

`input_file()` still uses global configuration and parser state.
Do not run an independent input operation while `input_file()` is active.
Do not start an input operation from an input callback.
Set log configuration before concurrent operations start.

### Internal time functions

The file `src/timeutil.h` declares internal functions such as `now_ms()`, `now_us()`, `str_to_timestamp()`, and `timestamp_to_str()`.
These functions do not use the `input_osm` namespace.
The public package does not install this header.

## 7. Examples

### 7.1 Count entities

The example in `test/integration/count_all.cpp` uses a different counter for each thread and entity type.
The `Counter` type is in `test/integration/counter.h`.
Displayed counts use fixed comma groups, such as `10,846,489,004`.
IDs and CSV numbers do not use comma groups.
The output uses fmt and does not require system locale settings.

To measure the run time in Bash, use this command:

```bash
time ./build/test/integration/count_all path/to/planet.osm.pbf
```

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

### 7.2 Set a log callback

The example in `test/integration/custom_log.cpp` adds a timestamp to each log message.
Use this pattern to set a callback:

```cpp
const auto logWithTime = [](input_osm::log_level_t level, const char* msg){ /* Print the message in the required format. */ };
input_osm::set_log_level(input_osm::LOG_LEVEL_TRACE);
input_osm::set_log_callback(logWithTime);
```

### 7.3 Collect statistics

This code from `test/integration/statistics.cpp` collects counts, maximum values, and timestamps.
It uses a different array entry for each thread.
The `block_index` variable identifies this PBF block.
The counter types are in `test/integration/counter.h`.
Include `<fmt/format.h>` and `<fmt/chrono.h>` for this example.

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
             &max_node_id](std::span<const input_osm::node_t> node_list) noexcept -> bool {
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
             &max_way_id](std::span<const input_osm::way_t> way_list) noexcept -> bool {
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
             &max_relation_id](std::span<const input_osm::relation_t> relation_list) noexcept -> bool {
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
        fmt::print(stderr, "Error while processing pbf\n");
        return EXIT_FAILURE;
    }

    fmt::print("nodes: {}\n", fmt::group_digits(std::accumulate(node_count.begin(), node_count.end(), 0LLU)));
    fmt::print("ways: {}\n", fmt::group_digits(std::accumulate(way_count.begin(), way_count.end(), 0LLU)));
    fmt::print("relations: {}\n", fmt::group_digits(std::accumulate(relation_count.begin(), relation_count.end(), 0LLU)));

    fmt::print("max nodes per block: {}\n",
               fmt::group_digits<uint64_t>(*std::max_element(max_node_count.begin(), max_node_count.end())));
    fmt::print("max node tags per block: {}\n",
               fmt::group_digits<uint64_t>(*std::max_element(max_node_tag_count.begin(), max_node_tag_count.end())));

    fmt::print("max ways per block: {}\n",
               fmt::group_digits<uint64_t>(*std::max_element(max_way_count.begin(), max_way_count.end())));
    fmt::print("max way tags per block: {}\n",
               fmt::group_digits<uint64_t>(*std::max_element(max_way_tag_count.begin(), max_way_tag_count.end())));
    fmt::print("max way nodes per block: {}\n",
               fmt::group_digits<uint64_t>(*std::max_element(max_way_node_count.begin(), max_way_node_count.end())));

    fmt::print("max relations per block: {}\n",
               fmt::group_digits<uint64_t>(*std::max_element(max_relation_count.begin(), max_relation_count.end())));
    fmt::print("max relation tags per block: {}\n",
               fmt::group_digits<uint64_t>(*std::max_element(max_relation_tag_count.begin(),
                                                             max_relation_tag_count.end())));
    fmt::print("max relation members per block: {}\n",
               fmt::group_digits<uint64_t>(*std::max_element(max_relation_member_count.begin(),
                                                             max_relation_member_count.end())));

    auto timestamp_to_str = [](const time_t in_time_t) -> std::string {
        return fmt::format("{:%F %T} GMT", fmt::gmtime(in_time_t));
    };

    fmt::print("max node timestamp: {}\n",
               timestamp_to_str(*std::max_element(node_timestamp.begin(), node_timestamp.end())));
    fmt::print("max way timestamp: {}\n", timestamp_to_str(*std::max_element(way_timestamp.begin(), way_timestamp.end())));
    fmt::print("max relation timestamp: {}\n",
               timestamp_to_str(*std::max_element(relation_timestamp.begin(), relation_timestamp.end())));

    fmt::print("max file block index: {}\n",
               static_cast<uint64_t>(*std::max_element(block_index.begin(), block_index.end())));

    fmt::print("nodes with tags: {}\n",
               fmt::group_digits(std::accumulate(node_with_tags_count.begin(), node_with_tags_count.end(), 0LLU)));
    fmt::print("ways with tags: {}\n",
               fmt::group_digits(std::accumulate(ways_with_tags_count.begin(), ways_with_tags_count.end(), 0LLU)));
    fmt::print(
        "relations with tags: {}\n",
        fmt::group_digits(std::accumulate(relations_with_tags_count.begin(), relations_with_tags_count.end(), 0LLU)));

    fmt::print("max node id: {}\n", static_cast<int64_t>(*std::max_element(max_node_id.begin(), max_node_id.end())));
    fmt::print("max way id: {}\n", static_cast<int64_t>(*std::max_element(max_way_id.begin(), max_way_id.end())));
    fmt::print("max relation id: {}\n",
               static_cast<int64_t>(*std::max_element(max_relation_id.begin(), max_relation_id.end())));
```

## 8. Logs and diagnostics

To enable trace messages, use this command:

```cpp
input_osm::set_log_level(input_osm::LOG_LEVEL_TRACE);
```

To receive log messages, set a callback with `set_log_callback()`.
Multiple threads can call this callback at the same time.
The callback must be thread-safe.
Keep the callback short to limit delays in the reader.
If message formatting is slow, put messages in a queue for processing by a different thread.

The default log callback writes to standard output.
To disable log messages, use `set_log_level(LOG_LEVEL_DISABLED)` in the `input_osm` namespace.
The `set_verbose()` function sets a flag that the reader does not use.

## 9. Performance and benchmarks

The recorded benchmark used the planet file dated 2022-09-05.
The system had two Xeon E5-2699 processors and 72 threads in total.

```
real    0m28.215s
user    30m23.402s
sys     0m18.354s
```

The benchmark output included these values:

```
nodes: 7,894,460,004
ways: 884,986,817
relations: 10,199,553
max nodes per block: 16,000
max way nodes per block: 833,428
```

The user time is greater than the elapsed time because multiple threads can use the processors at the same time.
File input, decompression, and callbacks can limit throughput.

### Increase throughput

- Use a Release build with compiler optimization.
- Use fast storage, such as NVMe storage or a RAM disk.
- If other processes use much processor time, set the CPU affinity for the reader.
- Keep callback work short.
- Process groups of entities together when possible.

## 10. Architecture

### PBF reader

1. The reader maps the file into memory.
2. The reader checks the header before it adds data blocks to a bounded work queue.
3. Worker threads get blocks from the queue and decompress the data when necessary.
4. Each worker decodes entities into vectors for that thread.
5. Each worker calls one handler for the complete decoded block.
6. For `input_file()`, the reader delivers primitive-group spans through an entity adapter.

An indexed read uses the offset table or scans headers from the file start.
It decodes only the requested data block in the calling thread.

### XML reader

Expat parses the XML file in the calling thread.
The reader collects the data for one entity at a time.
It calls the applicable handler with a span that contains that entity.

The reader uses small entity structures. It uses the same memory again for PBF batches.
It stores IDs and raw coordinates as integers.
Refer to section 11 for coordinate conversion.

## 11. Questions and answers

### How do I stop before the end of a file?

Return `false` from a handler to tell the reader to stop.
For PBF input, all read methods return `false` after a stop request or an error.
All workers stop before the input function returns.
Callbacks with prior permission can still finish during cancellation.
With one thread, no subsequent callback runs after a stop request.
The Boolean result does not distinguish cancellation from an error.

### Do tag strings end with a null character?

Tag fields and relation roles use `std::string_view` without a null-termination guarantee.
Use view comparisons and `.size()` to read their contents.
Do not pass `.data()` alone to a function that requires a C string.
Use these views only during the callback.

### How do I convert raw coordinates to degrees?

For XML input, multiply `raw_latitude` and `raw_longitude` by `1e-7`.
For example, use `double lat = raw_latitude * 1e-7;`.

For PBF input, the reader gives the raw coordinate integers.
The same conversion applies when the granularity is 100 nanodegrees and the coordinate offsets are zero.
Make sure that your source data has these properties before you use that conversion.

### How do I read OSC changes?

Read the `.osc` file with `input_file()`.
During a callback, `osc_mode` identifies the change operation.
The `mode_t` values are `bulk`, `create`, `modify`, and `destroy`.
The XML `delete` operation uses `mode_t::destroy`.

### How do I use a relation with many members?

Read the members through `relation_t::members` during the callback.
To use the members after the callback, copy them and their role strings before the callback returns.
The size limit for `relation_t` does not limit the number of members.

## 12. Contribute

Useful contributions include:

- Issues with steps to reproduce a problem and a small data sample
- Pull requests for one change
- Performance measurements
- More benchmarks or file formats

For changes to the project:

- Use ASD-STE100 Simplified Technical English for all technical documents and code comments, as required by [AGENTS.md](AGENTS.md).
- Keep public structures stable. Propose changes in an issue first.
- Add tests for new logic.
- Add an integration example for a change to the public API.
- Use the project code style.
- Enable `ENABLE_CLANG_TIDY` when the tool is available.

To do the build checks:

1. Build the unit tests and integration examples with `BUILD_TESTING=ON` and `INPUTOSM_INTEGRATION_TESTS=ON`.
2. Start `ctest --test-dir build --output-on-failure`.
3. Start the applicable integration examples with test data.

## 13. License

The project uses the Apache License 2.0.
Refer to the [LICENSE](LICENSE) file.
