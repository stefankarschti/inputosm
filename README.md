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
6. [API description](#6-api-description)
7. [Examples and best practices](#7-examples)
8. Logs and diagnostics
9. Performance and benchmarks
10. Architecture
11. Questions and answers
12. Contribute
13. License

## 1. Features

- Read PBF data with multiple threads.
- Read individual PBF data blocks by file index, with an optional offset table.
- Count entities without constructing entity arrays.
- Decode selected entity columns, strings, and header metadata on demand.
- Read OSM data and OSC changes in XML format.
- Get a span of entities in each callback. A span gives access to a sequence of objects in adjacent memory locations.
- Use different callbacks for nodes, ways, and relations.
- Select metadata, such as versions, timestamps, and changesets, for PBF decoding.
- Set a callback for log messages.
- Use Expat, libdeflate, and fmt as the library dependencies.
- Use node, way, and relation structures that each occupy a maximum of 64 bytes.

## 2. Start

This example uses the `input_file()` compatibility API with one thread.
It accepts PBF and XML input.
For PBF counts without entity decoding, use the [block API example](#71-count-entities).

```cpp
#include <inputosm/inputosm.hpp>
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
The declarations are in [inputosm.hpp](include/inputosm/inputosm.hpp).

| Interface | Use |
| --- | --- |
| `input_file()` | Read PBF or XML into entity records with text tags. Preserve existing application callbacks. |
| `pbf_reader_t` | Read PBF blocks, count entities, or decode selected columns. Support independent readers and random block access. |

PBF reference: [reader operations](#read-pbf-blocks-on-demand), [block methods](#block-methods-and-counts), [field options](#select-entity-fields),
[strings and metadata](#strings-and-entity-metadata), [header fields](#read-header-metadata),
[random access](#read-a-block-by-index), and [lifetimes and failures](#validation-failures-and-data-lifetime).

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

`pbf_reader_t` reads PBF files through opaque `pbf_block_t` references.
The block contents remain private.
The reader decodes data only when a block method requests it.
Use [block counting](#74-count-data-blocks) when you do not need entity data.
Use [entity counting](#71-count-entities) when you need counts without entity arrays.

| Reader method | Behavior |
| --- | --- |
| `open(filename)` | Open the file mapping. Check the initial header and required features. Return `false` if the reader is already open. |
| `close()` | Release this reader's file reference and index. Call only while the reader is idle. |
| `is_open()` | Report whether a file is open. |
| `set_thread_count(count)` | Set the worker count for `read_blocks()`. Zero selects one worker. The hardware thread count is the maximum. |
| `set_max_thread_count()` | Select the hardware thread count. |
| `thread_count()` | Return this reader's worker count. The default is one. |
| `read_blocks(handler)` | Visit each `OSMData` block from the file start. Skip the header and unknown block types. |
| `read_block(index, handler)` | Visit one data block in the calling thread. Use a file block index. |
| `decode_header(handler)` | Decode header metadata. Call the handler once in the calling thread. |
| `build_index()` | Build an optional file offset table. Repeated calls reuse the completed index. |
| `has_index()` | Report whether this reader has an index. |
| `index_memory_bytes()` | Return the allocated index capacity in bytes. Exclude the file mapping and decode buffers. |

Read and decode handlers return `bool`.
Supply a nonempty handler for each read or decode operation.
Return `true` to continue.
Return `false` to stop the operation.
Check every operation result before using its output.
The Boolean result does not distinguish a stop request from an error.
Reader destruction closes the reader.
Reader objects support moves but do not support copies.
Move or destroy a reader only while it is idle.

The reader supports raw and Zlib Blobs, ordinary nodes, and dense nodes.
It checks required header features during `open()`.
It rejects unsupported required features, including `HistoricalInformation`.
Full header metadata decoding remains explicit.

With one thread, block callbacks run in file order in the calling thread.
With multiple threads, callbacks can overlap and arrive out of file order.
Each block method and its callbacks run synchronously in that block callback's thread.
A decode method does not start more workers.
Keep the borrowed block reference in its callback thread.

Use one operation at a time on each reader.
Use a separate reader for a nested read or a simultaneous operation.
Readers of the same unchanged file share one immutable mapping.
Keep the file contents unchanged while any reader remains open.
Reader thread settings are independent of the global settings for `input_file()`.

### Block methods and counts

All block methods require an active block callback.
The callback receives `const pbf_block_t&`.
The block cannot be copied.

| Block method | Result |
| --- | --- |
| `index()` | File block index. The initial header has index zero. Unknown block types also occupy indexes. |
| `file_offset()` | Byte offset of the block's four-byte length field. |
| `parameters(result)` | Coordinate and timestamp parameters in `pbf_parameters_t`. |
| `string_table_size(result)` | Number of string table entries, including entry zero. |
| `node_count(result)` | Number of ordinary and dense nodes combined. |
| `way_count(result)` | Number of ways. |
| `relation_count(result)` | Number of relations. |
| `counts(result)` | All three entity counts in `pbf_counts_t`. |
| `decode_strings(handler)` | Strings in table order through `bool(std::string_view)`. |
| `decode_nodes(options, handler)` | Selected node columns through `bool(const pbf_node_batch_t&)`. |
| `decode_ways(options, handler)` | Selected way columns through `bool(const pbf_way_batch_t&)`. |
| `decode_relations(options, handler)` | Selected relation columns through `bool(const pbf_relation_batch_t&)`. |
| `decode_entities(options, handler)` | Selected entity types through `bool(const pbf_group_batch_t&)`. |
| `validate()` | Check supported strings and entity encodings without application entity callbacks. |

Count and parameter queries return `bool` and write through an output reference.
Individual counts and the string table size use `size_t`.
`pbf_counts_t` contains `nodes`, `ways`, and `relations`.

`index()` and `file_offset()` do not inspect the data payload.
Other block methods decompress the payload when necessary.
The block keeps decompressed bytes and completed counts until its block callback returns.
Repeated count queries reuse completed results.
Dense node counts scan encoded IDs without calculating absolute IDs.
`counts()` combines all three counts in one group traversal.
Separate count methods can require separate traversals.

A data block can contain multiple primitive groups.
Add batch totals across these groups to calculate a complete block total.
A batch size describes one group.
See the [statistics example](#73-collect-statistics) for complete block maxima.

### Select entity fields

Options select output columns independently.
Node decoding with default options supplies IDs only.
Way and relation decoding always supplies IDs.
All other options default to `false`.

| Options type | Option | Output column |
| --- | --- | --- |
| `pbf_node_options_t` | `id`, default `true` | `ids`: absolute signed 64-bit IDs. |
| `pbf_node_options_t` | `latitude` | `raw_latitudes`: signed 64-bit raw coordinates. |
| `pbf_node_options_t` | `longitude` | `raw_longitudes`: signed 64-bit raw coordinates. |
| `pbf_node_options_t` | `tags` | `tags`: lists of `pbf_tag_ids_t`. |
| `pbf_node_options_t` | `metadata` | `metadata`: one `pbf_metadata_t` per node. |
| `pbf_way_options_t` | `tags` | `tags`: lists of `pbf_tag_ids_t`. |
| `pbf_way_options_t` | `node_refs` | `node_refs`: lists of absolute signed 64-bit node IDs. |
| `pbf_way_options_t` | `metadata` | `metadata`: one `pbf_metadata_t` per way. |
| `pbf_way_options_t` | `node_locations` | `node_locations`: lists of `pbf_location_t`. Also supplies `locations_present`. |
| `pbf_relation_options_t` | `tags` | `tags`: lists of `pbf_tag_ids_t`. |
| `pbf_relation_options_t` | `member_ids` | `member_ids`: absolute signed 64-bit member IDs. |
| `pbf_relation_options_t` | `member_types` | `member_types`: `pbf_member_type_t::node`, `way`, or `relation`. |
| `pbf_relation_options_t` | `member_roles` | `member_roles`: unsigned 32-bit role string IDs. |
| `pbf_relation_options_t` | `metadata` | `metadata`: one `pbf_metadata_t` per relation. |

Ordinary nodes and dense nodes use the same `pbf_node_batch_t` type.
Latitude decoding does not require longitude or ID output.
Location decoding does not require node reference output.
Relation member IDs, types, and roles have independent options.

#### Batch layout

Each typed batch contains `block_index`, `group_index`, `count`, `parameters`, and `fields`.
Group indexes start at zero within each block.
`fields` records the selected options.
Selected scalar columns contain `count` entries.
Unselected columns are empty.
Use `fields` to distinguish an unselected column from a selected column in an empty batch.

Tags, way references, and way locations use `pbf_list_view_t<T>`.
Each selected list contains `count + 1` offsets and one flat `values` span.
The first offset is zero.
The last offset equals `values.size()`.
`batch.tags[i]` supplies the tags for entity `i`.
`batch.node_refs[i]` supplies the references for way `i`.
Use list indexing only when the corresponding field is selected.

Relation member columns share `member_offsets`.
If any member option is selected, this span contains `count + 1` offsets.
Relation `i` uses member indexes from `member_offsets[i]` through `member_offsets[i + 1] - 1`.
Selected member columns preserve member order.
The decoder checks that all three encoded member columns have equal lengths.

When way locations are selected, `locations_present[i]` reports encoded location field presence.
An absent location list is empty.
A present list can also be empty for a way without references.
Present latitude, longitude, and reference lists must have equal lengths.
The decoder requires the header's `LocationsOnWays` declaration when location fields are present.
It does not fetch coordinates from referenced nodes.

#### Combined decoding

`pbf_entity_options_t` contains optional `nodes`, `ways`, and `relations` selections.
Default construction disables all three entity types.
Assign an options object to enable an entity type.
Use `std::nullopt` to disable it.
Assigning `pbf_node_options_t{}` enables node IDs.

`decode_entities()` calls its handler once per primitive group.
`pbf_group_batch_t::kind` identifies `empty`, `nodes`, `dense_nodes`, `ways`, or `relations`.
Use only the typed batch for the reported kind and an enabled entity type.
Disabled types have empty output, even when the group contains those entities.
An empty group can still cause a callback.

The individual decode methods visit only matching groups.
Groups and their entities retain file order within each block.
An empty matching group can supply an empty batch.
A block without groups supplies no entity callbacks.
Repeated decode calls decode the selected columns again.
For several entity types, use [combined decoding](#78-decode-several-entity-types) to reduce repeated group traversal.

### Strings and entity metadata

The string table belongs to one block.
`decode_strings()` supplies every string in order, including the empty entry at index zero.
The callback has no string ID argument.
Its position gives the implicit ID.
`string_table_size()` supplies the entry count without constructing a string-view array.

Tags contain `pbf_tag_ids_t::key` and `value` string IDs.
Metadata `user_sid` and relation `member_roles` also contain string IDs.
The reader does not check these IDs against the string table size.
Check every ID before using it for string lookup.
The same string ID can refer to different text in different blocks.
If only IDs are needed, omit string decoding.
See [way tags and references](#76-decode-way-tags-and-node-references) for checked string lookup.

`pbf_metadata_t::present` identifies encoded fields.
Test the corresponding presence bit before interpreting an optional field.
Selected metadata contains one record per entity, including entities without metadata.

| Field | Type | Presence bit | Value when absent |
| --- | --- | --- | --- |
| `version` | `int32_t` | `version_present` | `-1` |
| `raw_timestamp` | `int64_t` | `timestamp_present` | `0` |
| `changeset` | `int64_t` | `changeset_present` | `0` |
| `uid` | `int32_t` | `uid_present` | `0` |
| `user_sid` | `uint32_t` | `user_present` | `0` |
| `visible` | `bool` | `visible_present` | `true` |

The presence bits belong to `pbf_metadata_t`.
A clear bit distinguishes absence from an encoded default value.
The metadata option selects all six fields together.

#### Coordinate and timestamp conversion

Entity batches supply `pbf_parameters_t` through `batch.parameters`.
`block.parameters(result)` supplies the same values before entity decoding.
The defaults are `granularity = 100`, zero coordinate offsets, and `date_granularity = 1000`.
Use the parameters from the current block.

| Converted value | Formula |
| --- | --- |
| Latitude in nanodegrees | `lat_offset + granularity * raw_latitude` |
| Longitude in nanodegrees | `lon_offset + granularity * raw_longitude` |
| Entity timestamp in Unix milliseconds | `date_granularity * raw_timestamp` |

Coordinate columns and way locations contain raw values after delta decoding.
For degrees, multiply nanodegrees by `1e-9`.
Use arithmetic that can represent the converted result.
Convert operands to floating point before multiplication when an approximate degree value is sufficient.
For exact integer conversion, check multiplication and addition for overflow.

### Read header metadata

Call `reader.decode_header(handler)` while the reader is idle.
The handler receives `const pbf_header_metadata_t&`.
The reader decodes these fields on demand after the feature checks during `open()`.

| Header field | Representation and meaning |
| --- | --- |
| `bbox` | Optional `pbf_header_bbox_t` with signed 64-bit `left`, `right`, `top`, and `bottom` nanodegrees. |
| `required_features` | Span of required feature string views. |
| `optional_features` | Span of optional feature string views. |
| `writingprogram` | Optional program name string view. |
| `source` | Optional source string view. |
| `osmosis_replication_timestamp` | Optional signed 64-bit Unix timestamp in seconds. |
| `osmosis_replication_sequence_number` | Optional signed 64-bit replication sequence number. |
| `osmosis_replication_base_url` | Optional replication URL string view. |

Optional values distinguish absence from empty strings and numeric zero.
Header coordinates and replication timestamps do not use data block conversion parameters.
Header views remain valid only during their callback.
See the [header example](#79-read-header-fields) for presence checks.

### Read a block by index

Use `read_block()` with a file block index from an earlier block callback.
The method calls its handler once in the calling thread, regardless of the reader's worker count.
Header blocks, unknown block types, and unavailable indexes return `false` without a callback.
Data block indexes can have gaps.
Do not treat a callback count as the largest file block index.

| Access mode | Setup and cost |
| --- | --- |
| Without an index | Each request scans earlier file block headers and skips their payloads. No offset table is allocated. |
| With `build_index()` | Scan all file block headers once. Subsequent requests use the selected offset directly. |

Each reader keeps its own offset table.
The table stores a 64-bit offset for each file block, including the header and unknown block types.
`index_memory_bytes()` includes unused allocated capacity.
Index construction checks file framing without decoding data payloads.
An index does not cache decoded blocks or accelerate `read_blocks()`.
Use an index for repeated random requests when its setup and memory costs are acceptable.
For occasional requests, compare both modes with the [benchmark tools](test/benchmark/README.md).
See the [random access example](#710-read-selected-blocks).

### Validation, failures, and data lifetime

A block callback can succeed without inspecting its compressed payload.
Count queries inspect only the structures needed for those counts.
Selective decoding checks selected fields and their structural dependencies.
It does not fully validate unrequested fields.

Use `block.validate()` when all supported entity encodings must be checked.
This method also checks the string table structure.
It does not check entity string IDs against the string table size.
It does not check whether referenced OSM entities exist.
Validation requires decoding work and can remove the speed benefit of selective access.

A failed block method marks the current block operation as failed.
Ignoring that failure does not make the enclosing read succeed.
Return each decode result from the block callback.
Callback exceptions become operation failures.
With multiple workers, callbacks that already started can finish during cancellation.
All workers stop before the read method returns.

| Borrowed object | Valid until |
| --- | --- |
| `pbf_block_t` reference | Its block callback returns. |
| Entity batch, column spans, and list views | Their entity callback returns. |
| String bytes from `decode_strings()` | The enclosing block callback returns. |
| Header spans and string views | The header callback returns. |

Copy entity data into application storage before its entity callback returns.
Copy strings into `std::string` when they must survive their permitted lifetime.
Copying a span or string view does not copy its data.
Do not send a borrowed block or batch to another thread.
For asynchronous processing, copy the required values first.

Call string decoding before entity decoding when callbacks need string lookup.
Do not call methods on the same block from its entity or string callback.
`index()` and `file_offset()` are the exceptions.
Use `batch.parameters` inside an entity callback.
Internal thread-local buffers retain capacity for reuse.
Larger groups can require further buffer allocations.
A nested read through another reader uses separate internal buffers.

### Migrate to version 0.4.0

The opaque block replaces the eager block structure.
Rebuild direct block consumers with the new headers and library.
Replace `<inputosm/inputosm.h>` with `<inputosm/inputosm.hpp>` in application includes.
Replace the removed `input_pbf_blocks()` function with `pbf_reader_t::open()` and `read_blocks()`.

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

For `input_file()`, use entity spans and their data only during the callback that receives them.
This limit includes tag strings and relation roles in legacy entity records.
To use data after the callback returns, copy the data into memory that your application owns.
A copy of a structure alone does not copy the data to which its spans and string views refer.
For the block API, use the [lifetime table](#validation-failures-and-data-lifetime).

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

The file `src/timeutil.hpp` declares internal functions such as `now_ms()`, `now_us()`, `str_to_timestamp()`, and `timestamp_to_str()`.
These functions do not use the `input_osm` namespace.
The public package does not install this header.

## 7. Examples

The PBF helpers below use these headers:

```cpp
#include <inputosm/inputosm.hpp>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include <fmt/format.h>
```

Helpers that accept `pbf_reader_t&` require an open reader.
Call `reader.open(path)` before calling those helpers.
Call `reader.set_max_thread_count()` to enable multiple workers.
Check the returned Boolean result.
Link the application to `inputosm::inputosm` and `fmt::fmt`.

Examples: [entity counts](#71-count-entities), [statistics](#73-collect-statistics), [block counts](#74-count-data-blocks),
[node positions](#75-decode-node-ids-and-positions), [way tags](#76-decode-way-tags-and-node-references),
[relations](#77-decode-relations-with-all-fields), [combined decoding](#78-decode-several-entity-types),
[header fields](#79-read-header-fields), and [random access](#710-read-selected-blocks).
See [best practices](#711-best-practices) for field selection, storage, and threads.

### 7.1 Count entities

The [count_entity program](test/integration/count_entity.cpp) uses a different counter for each thread and entity type.
It uses `pbf_reader_t::read_blocks()` and `block.counts()` to count nodes, ways, and relations in one group traversal.
It does not construct entity arrays or decode metadata.
The example below uses aligned worker storage to reduce cache contention.
Displayed counts use fixed comma groups, such as `10,846,489,004`.
IDs and CSV numbers do not use comma groups.
The output uses fmt and does not require system locale settings.

To measure the run time in Bash, use this command:

```bash
time ./build/test/integration/count_entity path/to/planet.osm.pbf
```

```cpp
bool count_entities(const char* path)
{
    input_osm::pbf_reader_t reader;
    reader.set_max_thread_count();
    if (!reader.open(path)) return false;
    struct alignas(64) totals_t
    {
        uint64_t nodes = 0, ways = 0, relations = 0;
    };
    std::vector<totals_t> workers(reader.thread_count());
    if (!reader.read_blocks([&](const input_osm::pbf_block_t& block) {
            input_osm::pbf_counts_t counts;
            if (!block.counts(counts)) return false;
            auto& totals = workers[input_osm::thread_index];
            totals.nodes += counts.nodes;
            totals.ways += counts.ways;
            totals.relations += counts.relations;
            return true;
        }))
        return false;

    totals_t result;
    for (const auto& worker : workers)
    {
        result.nodes += worker.nodes;
        result.ways += worker.ways;
        result.relations += worker.relations;
    }
    fmt::print("Nodes: {} Ways: {} Relations: {}\n",
               fmt::group_digits(result.nodes),
               fmt::group_digits(result.ways),
               fmt::group_digits(result.relations));
    return true;
}
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

### 7.4 Count data blocks

This example counts data callbacks without decompressing data payloads.
It uses one thread because the callback only increments a counter.
The count excludes the header and unknown block types.

```cpp
bool count_data_blocks(const char* path)
{
    input_osm::pbf_reader_t reader;
    if (!reader.open(path)) return false;
    uint64_t count = 0;
    if (!reader.read_blocks([&](const input_osm::pbf_block_t&) {
            ++count;
            return true;
        }))
        return false;
    fmt::print("Data blocks: {}\n", count);
    return true;
}
```

The [count_blocks program](test/integration/count_blocks.cpp) also demonstrates separate counters with multiple workers.

### 7.5 Decode node IDs and positions

This example selects IDs, latitude, and longitude.
It handles ordinary and dense nodes through the same callback.
The calculation uses floating point before multiplication to avoid integer overflow.

```cpp
bool read_node_positions(input_osm::pbf_reader_t& reader)
{
    return reader.read_blocks([](const input_osm::pbf_block_t& block) {
        return block.decode_nodes(
            {.id = true, .latitude = true, .longitude = true},
            [](const input_osm::pbf_node_batch_t& batch) {
                const auto& p = batch.parameters;
                for (size_t i = 0; i < batch.count; ++i)
                {
                    const double latitude =
                        (double(p.lat_offset) + double(p.granularity) * double(batch.raw_latitudes[i])) * 1e-9;
                    const double longitude =
                        (double(p.lon_offset) + double(p.granularity) * double(batch.raw_longitudes[i])) * 1e-9;
                    fmt::print("{} {:.7f} {:.7f}\n", batch.ids[i], latitude, longitude);
                }
                return true;
            });
    });
}
```

For latitude alone, select `{.id = false, .latitude = true}`.
The `ids` and `raw_longitudes` spans then remain empty.

### 7.6 Decode way tags and node references

This example constructs a string-view table for each block that contains ways.
It checks string IDs before lookup.
Each worker reuses its own vector capacity across blocks.
The vectors belong to this operation, so separate readers do not share application buffers.

```cpp
bool read_way_tags(input_osm::pbf_reader_t& reader)
{
    std::vector<std::vector<std::string_view>> tables(reader.thread_count());
    return reader.read_blocks([&](const input_osm::pbf_block_t& block) {
        size_t ways = 0;
        if (!block.way_count(ways)) return false;
        if (ways == 0) return true;

        auto& strings = tables[input_osm::thread_index];
        strings.clear();
        const bool ok = block.decode_strings([&](std::string_view value) {
            strings.push_back(value);
            return true;
        }) && block.decode_ways(
            {.tags = true, .node_refs = true},
            [&](const input_osm::pbf_way_batch_t& batch) {
                for (size_t i = 0; i < batch.count; ++i)
                {
                    fmt::print("Way {}\n", batch.ids[i]);
                    for (const auto tag : batch.tags[i])
                    {
                        if (tag.key >= strings.size() || tag.value >= strings.size()) return false;
                        fmt::print("  {}={}\n", strings[tag.key], strings[tag.value]);
                    }
                    for (const auto id : batch.node_refs[i])
                        fmt::print("  Node {}\n", id);
                }
                return true;
            });
        strings.clear();
        return ok;
    });
}
```

The preliminary way count avoids string decoding in blocks without ways.
It adds a count traversal in blocks with ways.
Measure this tradeoff for the input file.

For a known capacity requirement, call `string_table_size(size)` before `decode_strings()`.
Then use `strings.reserve(size)`.
The initial size query scans the string table.
Without that requirement, vector growth avoids the preliminary string scan.

To request optional way coordinates, also set `.node_locations = true`.
Check `batch.locations_present[i]` before using `batch.node_locations[i]`.
Each location contains `raw_latitude` and `raw_longitude`.
Convert them with `batch.parameters`, as in the node example.
Copy required references into an owned vector before the entity callback returns.

### 7.7 Decode relations with all fields

This example selects tags, all member columns, and metadata.
It prints string IDs without string lookup.
For role text, construct the block's string table before entity decoding.
Check each role ID before lookup.

```cpp
bool read_relations(input_osm::pbf_reader_t& reader)
{
    return reader.read_blocks([](const input_osm::pbf_block_t& block) {
        return block.decode_relations(
            {.tags = true, .member_ids = true, .member_types = true,
             .member_roles = true, .metadata = true},
            [](const input_osm::pbf_relation_batch_t& batch) {
                for (size_t i = 0; i < batch.count; ++i)
                {
                    fmt::print("Relation {}\n", batch.ids[i]);
                    for (const auto tag : batch.tags[i])
                        fmt::print("  Tag IDs {}={}\n", tag.key, tag.value);
                    const size_t begin = batch.member_offsets[i];
                    const size_t end = batch.member_offsets[i + 1];
                    for (size_t j = begin; j < end; ++j)
                        fmt::print("  Member {} type={} role_sid={}\n",
                                   batch.member_ids[j],
                                   static_cast<unsigned>(batch.member_types[j]),
                                   batch.member_roles[j]);

                    const auto& metadata = batch.metadata[i];
                    if (metadata.present & input_osm::pbf_metadata_t::timestamp_present)
                    {
                        const long double seconds =
                            static_cast<long double>(metadata.raw_timestamp) *
                            batch.parameters.date_granularity / 1000.0L;
                        fmt::print("  Unix timestamp: {:.3f} seconds\n", seconds);
                    }
                    if (metadata.present & input_osm::pbf_metadata_t::user_present)
                        fmt::print("  User string ID: {}\n", metadata.user_sid);
                }
                return true;
            });
    });
}
```

The remaining metadata fields are available in each `metadata` record.
Check their presence bits before use.
Setting `.metadata = false` removes the metadata column and its decoding work.

### 7.8 Decode several entity types

This helper selects all entity columns in one group traversal.
Its `with_metadata` argument controls metadata for all three types.
The application supplies a `pbf_group_handler_t` callback to process each group.

```cpp
bool read_all_columns(input_osm::pbf_reader_t& reader,
                      bool with_metadata,
                      const input_osm::pbf_group_handler_t& process_group)
{
    const input_osm::pbf_entity_options_t options{
        .nodes = input_osm::pbf_node_options_t{
            .id = true, .latitude = true, .longitude = true,
            .tags = true, .metadata = with_metadata},
        .ways = input_osm::pbf_way_options_t{
            .tags = true, .node_refs = true, .metadata = with_metadata,
            .node_locations = true},
        .relations = input_osm::pbf_relation_options_t{
            .tags = true, .member_ids = true, .member_types = true,
            .member_roles = true, .metadata = with_metadata}};

    return reader.read_blocks([&](const input_osm::pbf_block_t& block) {
        return block.decode_entities(options, process_group);
    });
}
```

In `process_group`, inspect `group.kind` before accessing `group.nodes`, `group.ways`, or `group.relations`.
Both `nodes` and `dense_nodes` kinds use `group.nodes`.
An `empty` group has no entity data.
The [statistics program](test/integration/statistics.cpp) shows this dispatch.
For a smaller field selection, change the options before the traversal.
If a type is not needed, leave its optional selection absent.

### 7.9 Read header fields

This example prints all header fields with presence checks.
Replication timestamps already use Unix seconds.

```cpp
bool print_header(input_osm::pbf_reader_t& reader)
{
    return reader.decode_header([](const input_osm::pbf_header_metadata_t& header) {
        if (header.bbox)
        {
            const auto& box = *header.bbox;
            fmt::print("Bounds in nanodegrees: left={} right={} top={} bottom={}\n",
                       box.left, box.right, box.top, box.bottom);
        }
        for (const auto feature : header.required_features)
            fmt::print("Required feature: {}\n", feature);
        for (const auto feature : header.optional_features)
            fmt::print("Optional feature: {}\n", feature);
        if (header.writingprogram) fmt::print("Program: {}\n", *header.writingprogram);
        if (header.source) fmt::print("Source: {}\n", *header.source);
        if (header.osmosis_replication_timestamp)
            fmt::print("Replication timestamp: {}\n", *header.osmosis_replication_timestamp);
        if (header.osmosis_replication_sequence_number)
            fmt::print("Replication sequence: {}\n", *header.osmosis_replication_sequence_number);
        if (header.osmosis_replication_base_url)
            fmt::print("Replication URL: {}\n", *header.osmosis_replication_base_url);
        return true;
    });
}
```

### 7.10 Read selected blocks

Supply data block indexes recorded from `block.index()` during an earlier traversal.
This helper supports both random access modes.
It uses a fresh reader so `use_index = false` starts without an offset table.

```cpp
bool read_selected_blocks(const char* path,
                          std::span<const size_t> indexes,
                          bool use_index)
{
    input_osm::pbf_reader_t reader;
    if (!reader.open(path)) return false;
    if (use_index && !reader.build_index()) return false;
    fmt::print("Index capacity: {} bytes\n", reader.index_memory_bytes());

    for (const auto index : indexes)
    {
        if (!reader.read_block(index, [](const input_osm::pbf_block_t& block) {
                input_osm::pbf_counts_t counts;
                if (!block.counts(counts)) return false;
                fmt::print("Block {} at byte {}: {} nodes, {} ways, {} relations\n",
                           block.index(), block.file_offset(),
                           counts.nodes, counts.ways, counts.relations);
                return true;
            }))
            return false;
    }
    return true;
}
```

For concurrent random requests, give each application thread its own reader.
Open all readers before processing requests to share the file mapping.
Build each reader's index when indexed access is needed.
`read_block()` uses `thread_index == 0` within each reader's callback.
Keep results separate for each application thread.
Setting the reader worker count does not parallelize a random request.

### 7.11 Best practices

- Select only the fields that the application uses.
- For block counts, count callbacks without calling payload methods.
- For all entity counts, use `counts()`.
- For one entity count, use its individual count method.
- For several entity types, use `decode_entities()` with one group traversal.
- Use the typed decode methods when only one entity type is needed.
- Decode strings only when the application needs text.
- Check string ID bounds before lookup.
- Process borrowed spans during their callback.
- Keep reusable application buffers separate for each reader and worker.
- Add group totals within each block before calculating block maxima.
- Use separate worker counters during iteration.
- Combine worker results after `read_blocks()` returns.
- Set thread and log configuration before operations start.
- Compare indexed and unindexed access for the expected random request count.
- Measure performance with the actual field selection and file.

The printing examples show field access.
Printing each entity can dominate execution time.
For throughput measurements, replace printing with the required batch processing.
Use one worker when output must follow file order.

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
With the block API, use `batch.parameters` and the [conversion formulas](#coordinate-and-timestamp-conversion).
The fixed `1e-7` conversion applies only when granularity is 100 nanodegrees and coordinate offsets are zero.
The legacy `input_file()` records do not expose those parameters.
Check the source data before using that fixed conversion with legacy PBF records.

### How do I read OSC changes?

Read the `.osc` file with `input_file()`.
During a callback, `osc_mode` identifies the change operation.
The `mode_t` values are `bulk`, `create`, `modify`, and `destroy`.
The XML `delete` operation uses `mode_t::destroy`.

### How do I use a relation with many members?

With `input_file()`, read members through `relation_t::members` during the callback.
To use the members after the callback, copy them and their role strings before the callback returns.
The size limit for `relation_t` does not limit the number of members.
With the block API, use selected member columns and `member_offsets`.
See the [relation example](#77-decode-relations-with-all-fields).

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
