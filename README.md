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
- Use Expat and Zlib as the library dependencies.
- Use node, way, and relation structures that each occupy a maximum of 64 bytes.

## 2. Start

This example counts entities with one thread.
Section 7.1 shows counters for multiple threads.

```cpp
#include <inputosm/inputosm.h>
#include <cstdint>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "Usage: demo <file.osm.pbf> [meta]\n"; return 1; }
    const char* file = argv[1];
    const bool read_meta = (argc >= 3);
    input_osm::set_thread_count(1); // Use one thread for these shared counters.

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
- A C++20 compiler
- An operating system with the POSIX interfaces that the source files use
- Expat and Zlib
- clang-tidy, unless `ENABLE_CLANG_TIDY` is `OFF`

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

To get the compiler and linker options with pkg-config, use this command:

```bash
pkg-config --cflags --libs inputosm
```

## 4. Conan usages

Conan can get the Expat and Zlib dependencies.

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

- `node_t { int64_t id; int64_t raw_latitude; int64_t raw_longitude; span_t<tag_t> tags; int32_t version; int32_t timestamp; int32_t changeset; }`
- `way_t { int64_t id; span_t<int64_t> node_refs; span_t<tag_t> tags; int32_t version; int32_t timestamp; int32_t changeset; }`
- `relation_t { int64_t id; span_t<relation_member_t> members; span_t<tag_t> tags; int32_t version; int32_t timestamp; int32_t changeset; }`
- `tag_t { const char* key; const char* value; }`
- `relation_member_t { uint8_t type; int64_t id; const char* role; }`

A relation member has type `0` for a node, `1` for a way, or `2` for a relation.

### Read a file

Use `input_file()` with a path, a metadata option, and three handlers.
A handler is a callback for one entity type.
Each handler has the type `std::function<bool(span_t<T>)>` for its entity type `T`.
A handler returns `true` to continue.
Refer to section 11 for the limits on cancellation.

The `decode_metadata` option controls PBF metadata decoding.
This option does not change how the XML reader reads metadata attributes.

### Configure threads and logs

| Function or variable | Description |
| --- | --- |
| `set_thread_count(size_t)` | Set the number of PBF threads. The hardware thread count is the maximum. |
| `set_max_thread_count()` | Select the hardware thread count for PBF input. |
| `thread_count()` | Get the configured thread count. The minimum result is 1. |
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
A copy of a structure alone does not copy the data to which its spans and pointers refer.

### Concurrent access

PBF workers can call the same handler at the same time.
Use a different counter for each thread.
If threads share data, use synchronization to prevent concurrent changes.
Use `thread_index` to select the array or vector entry for this thread.

The library also uses global configuration and parser state.
Do not call `input_file()` from different threads at the same time.
Set the thread configuration and log configuration before you read a file.

### Internal time functions

The file `src/timeutil.h` declares internal functions such as `now_ms()`, `now_us()`, `str_to_timestamp()`, and `timestamp_to_str()`.
These functions do not use the `input_osm` namespace.
The public package does not install this header.

## 7. Examples

### 7.1 Count entities

The example in `test/integration/count_all.cpp` uses a different counter for each thread and entity type.
The `Counter` type is in `test/integration/counter.h`.

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
2. The reader adds file blocks to a work queue.
3. Worker threads get blocks from the queue and decompress the data when necessary.
4. Each worker decodes entities into vectors for that thread.
5. Each worker calls the handlers with spans of entities.

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
This result does not guarantee that all PBF workers stop or that `input_file()` returns `false`.
Do not use the return value alone to identify cancellation.

### Do tag strings end with a null character?

Yes. Tag strings end with a null character.
Use these strings only during the callback.

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
