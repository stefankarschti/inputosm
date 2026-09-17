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

To build and use the supplied `count_entity` example on Linux:

1. Configure the build.

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   ```

2. Build the project.

   ```bash
   cmake --build build --parallel $(nproc)
   ```

3. Start `count_entity` with the path to a PBF file.

   ```bash
   ./build/test/integration/count_entity path/to/planet.osm.pbf
   ```

The supplied `count_entity` example uses multiple threads and a different counter for each thread.
It requests entity counts through the block API without decoding entity fields.

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

### Read PBF blocks on demand

Use `pbf_reader_t::read_blocks()` for one callback per `OSMData` block.
The callback receives an opaque block reference.
The reader decompresses its payload only when a block method needs that payload.
Count methods do not construct entity arrays.

```cpp
input_osm::pbf_reader_t reader;
reader.set_max_thread_count();
const bool ok = reader.open("map.osm.pbf") && reader.read_blocks(
    [](const input_osm::pbf_block_t& block) {
        input_osm::pbf_counts_t counts;
        if (!block.counts(counts)) return false;
        fmt::print("Block {}: {} nodes, {} ways, {} relations\n",
                   block.index(), counts.nodes, counts.ways, counts.relations);
        return true;
    });
```

`node_count()`, `way_count()`, and `relation_count()` request individual counts.
The first query inspects the payload. Later queries reuse completed counts.
Dense node counts scan encoded IDs without calculating absolute IDs.
A count query does not validate unrequested entity fields.

`block.index()` includes the initial header at index zero.
`block.file_offset()` identifies the four-byte length field.
`parameters()` supplies the coordinate and timestamp conversion parameters.
The reader supports raw and Zlib Blobs, ordinary nodes, and dense nodes.
It checks required header features during `open()`.
It rejects unsupported required features, including historical visibility.

With one thread, block callbacks run in file order in the calling thread.
With multiple threads, callbacks can overlap and finish in a different order.
Every decode method runs synchronously in the thread that calls it.
Keep the borrowed block reference in its callback thread.
Use a separate reader for a nested read or a simultaneous operation.
Readers of the same unchanged file share one immutable mapping.
Keep the file contents unchanged while any reader remains open.

### Select entity fields

Node options select IDs, latitude, longitude, tags, and metadata independently.
Way options select tags, references, metadata, and optional node locations.
Relation options select tags, member IDs, member types, member roles, and metadata independently.
Ways and relations always supply their IDs during entity decoding.

```cpp
const bool ok = reader.read_blocks([](const input_osm::pbf_block_t& block) {
    return block.decode_nodes(
        {.id = true, .latitude = true, .longitude = true},
        [](const input_osm::pbf_node_batch_t& batch) {
            for (size_t i = 0; i < batch.count; ++i)
                fmt::print("{} {} {}\n", batch.ids[i], batch.raw_latitudes[i], batch.raw_longitudes[i]);
            return true;
        });
});
```

Each callback receives spans for one primitive group.
Selected scalar columns have `batch.count` elements.
Unselected columns are empty.
Variable lists use flat values and offsets. For example, `batch.tags[i]` selects one entity's tags.
`decode_entities()` combines selections for all three entity types in one group traversal.

Tags, user names, and member roles use string IDs.
The reader does not check those IDs against the string table size.
The application must check bounds before string lookup.
`string_table_size()` returns the number of strings, including entry zero.
`decode_strings()` calls `bool(std::string_view)` for every string in table order.
Callback positions give implicit string IDs, starting at zero.

Entity spans remain valid only during their entity callback.
String byte views remain valid until the enclosing block callback returns.
Internal TLS buffers retain capacity for reuse.
A nested read through another reader uses separate buffers.
Block methods cannot be called recursively from an entity or string callback, except `index()` and `file_offset()`.

The new metadata type preserves 64-bit timestamps and changesets, user fields, visibility, and field presence.
Coordinates remain raw integers after delta decoding.
Convert latitude to nanodegrees with `lat_offset + granularity * raw_latitude`.
Use the equivalent longitude parameters for longitude.
Convert timestamps to Unix milliseconds with `date_granularity * raw_timestamp`.
Use arithmetic that can represent the result.

### Read header metadata

Call `reader.decode_header(handler)` while the reader is idle.
The handler receives `const pbf_header_metadata_t&` in the calling thread.
It exposes the bounding box, feature lists, writing program, source, and all three replication fields.
Optional values distinguish absence from empty strings and numeric zero.
Header views remain valid only during that callback.
Header bounding box coordinates use nanodegrees independently of data block parameters.

### Read a block by index

Use `read_block()` with a file block index from an earlier block callback.
The method calls the handler once in the calling thread.
Header blocks, unknown block types, and unavailable indexes return `false` without a callback.

```cpp
input_osm::pbf_reader_t reader;
if (!reader.open("map.osm.pbf")) return 1;
if (!reader.build_index()) return 1;
const bool ok = reader.read_block(100, [](const input_osm::pbf_block_t& block) {
    size_t count;
    if (!block.node_count(count)) return false;
    fmt::print("Block {} has {} nodes\n", block.index(), count);
    return true;
});
```

`build_index()` is optional.
Without it, each request scans earlier file block headers and skips their payloads.
With it, the reader uses an in-memory offset table.
`index_memory_bytes()` reports allocated index capacity.
Each reader keeps its own offset table.
Index construction validates framing without decoding data payloads.

### Migrate to version 0.4.0

The opaque block replaces the eager block structure.
Rebuild direct block consumers with the new headers and library.

1. Remove the metadata argument from `read_blocks()` and `read_block()`.
2. Replace entity span sizes with count methods.
3. Replace entity span access with the selected decode method.
4. Select metadata in the entity options when necessary.
5. Use `decode_strings()` when string lookup is necessary.
6. Check string ID bounds in the application before lookup.

The `input_file()` signature, entity types, callback order, and XML behavior remain unchanged.
Its PBF adapter uses public block iteration and the shared group decoder.
It writes legacy records directly and retains the existing string lookup and metadata checks.
The adapter preserves empty callback batches and validates entities when handlers are absent.
A callback failure stops subsequent callbacks for that group.
Callbacks that already started on other workers can finish during cancellation.

The [deferred reader design](docs/pbf-deferred-decoding-proposal.md) defines the complete contract.
The [benchmark guide](test/benchmark/README.md) describes field masks, count combinations, and baseline comparisons.
The [performance report](docs/pbf-deferred-performance.md) gives planet results and memory measurements with 32 threads.

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

The example in `test/integration/count_entity.cpp` uses a different counter for each thread and entity type.
It uses `pbf_reader_t::read_blocks()` and `block.counts()` to count nodes, ways, and relations in one group traversal.
It does not construct entity arrays or decode metadata.
The `Counter` type is in `test/integration/counter.h`.
Displayed counts use fixed comma groups, such as `10,846,489,004`.
IDs and CSV numbers do not use comma groups.
The output uses fmt and does not require system locale settings.

To measure the run time in Bash, use this command:

```bash
time ./build/test/integration/count_entity path/to/planet.osm.pbf
```

```cpp
input_osm::pbf_reader_t reader;
reader.set_max_thread_count();
const auto threads = reader.thread_count();
std::vector<input_osm::Counter<uint64_t>> counters(3 * threads);
auto nodes = std::span{counters.data(), threads};
auto ways  = std::span{counters.data() + threads, threads};
auto rels  = std::span{counters.data() + 2 * threads, threads};

const bool ok = reader.open(file) && reader.read_blocks([&](const input_osm::pbf_block_t& block) {
    input_osm::pbf_counts_t counts;
    if (!block.counts(counts)) return false;
    nodes[input_osm::thread_index] += counts.nodes;
    ways[input_osm::thread_index] += counts.ways;
    rels[input_osm::thread_index] += counts.relations;
    return true;
});
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

The [statistics example](test/integration/statistics.cpp) reports entity counts, block maxima, tagged entities, maximum IDs, and timestamps.
It uses `pbf_reader_t::read_blocks()` and one `decode_entities()` traversal for each block.

| Entity type | Selected fields |
| --- | --- |
| Nodes | IDs and tags. |
| Ways | IDs, tags, and node references. |
| Relations | IDs, tags, and member types. |

The member type column supplies the relation member count without decoding member IDs or roles.
The program uses tag IDs without string lookup.
It does not decode node coordinates or way locations.

Each block callback combines counts across all primitive groups before updating the block maxima.
Thus, the reported maxima apply to complete PBF data blocks.
Each worker uses separate statistics storage.
The program combines worker results after iteration finishes.

Run the example without metadata:

```bash
./build/test/integration/statistics path/to/planet.osm.pbf
```

Add the optional argument to decode metadata:

```bash
./build/test/integration/statistics path/to/planet.osm.pbf 1
```

Timestamp maxima use 64-bit values and each block's `date_granularity`.
The program displays Unix time in whole seconds with the GMT suffix.
Without metadata, timestamp maxima remain at the Unix epoch.

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

The [deferred decoding report](docs/pbf-deferred-performance.md) compares version 0.4.0 with `main` on the 2026 planet file.
It includes count combinations, selected entity fields, metadata, random access, and memory measurements with 32 threads.
The [benchmark guide](test/benchmark/README.md) explains how to measure other field combinations.

### Historical measurement

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

1. Readers of the same unchanged file share one file mapping.
2. The reader checks the header before it adds data block descriptors to a bounded work queue.
3. Each worker calls the block handler with an opaque block reference.
4. The first data request decompresses the block payload when necessary.
5. Count methods examine message structures without constructing entity arrays.
6. Decode methods write selected columns into reusable thread-local storage for one primitive group.
7. Entity callbacks run synchronously in the thread that requests decoding.
8. For `input_file()`, the compatibility adapter writes legacy records through the same block iteration API.

An indexed read uses the offset table or scans headers from the file start.
It supplies the requested data block in the calling thread.
Its payload remains undecoded until the callback requests data.

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
