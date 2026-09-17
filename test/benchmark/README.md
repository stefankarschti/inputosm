# PBF benchmarks

The [performance report](../../docs/pbf-deferred-performance.md) contains the recorded planet comparison.

Build the benchmark in Release mode.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DINPUTOSM_BENCHMARKS=ON
cmake --build build --target pbf_benchmark -j 16
```

The command syntax is:

```text
pbf_benchmark <file> <mode> <threads> <repetitions> [metadata=0] [requests=256] [max_index=0] [selection options]
```

| Mode | Operation |
| --- | --- |
| `entities` | Decode legacy entities through `input_file()`. |
| `blocks` | Iterate opaque blocks and request the selected counts or fields. |
| `random` | Scan file headers from the start for each random block request. |
| `random-index` | Build an offset table for each reader, then request random blocks directly. |

Random modes use separate readers and one shared file mapping.
They use the same SplitMix64 seed, `0x494e5055544f534d`, and the same request assignment.
`max_index` must identify the last data block in a file with consecutive data indexes from one.
The comparison script checks that condition before it starts random workloads.

## Select fields and counts

Masks can use decimal or hexadecimal notation.
Add the required bit values to select a combination.
For example, node mask `7` selects IDs, latitude, and longitude.

| Option | Bit values |
| --- | --- |
| `--nodes=MASK` | ID `1`; latitude `2`; longitude `4`; tags `8`; metadata `16`. |
| `--ways=MASK` | Tags `1`; node references `2`; metadata `4`; node locations `8`. Way IDs are always decoded. |
| `--relations=MASK` | Tags `1`; member IDs `2`; member types `4`; member roles `8`; metadata `16`. Relation IDs are always decoded. |
| `--counts=MASK` | Blocks `1`; nodes `2`; ways `4`; relations `8`. |
| `--strings` | Iterate every string in each block. |

Without selection options, the benchmark decodes all legacy-equivalent fields.
The positional metadata argument controls metadata for that default selection.
Default way decoding excludes optional node locations because the legacy interface does not expose them.

Any selection option disables the default entity selections.
Unspecified entity types remain disabled.
A zero entity mask enables that entity type with its optional fields disabled.
`--counts=0` performs framing-only block iteration.
The benchmark always records visited blocks for diagnostics, including when block counting is not selected.

Count and entity selections can be combined.
When both request an entity type, the benchmark checks that its count matches the decoded batch totals.
A combined node, way, and relation count uses `block.counts()`.
Other count combinations use the individual cached count methods.
When an application needs several counts, `counts()` avoids repeated group traversal.

Entity mode and eager baseline builds cannot select fields.
They reject selection options instead of silently ignoring them.

## Select callback work

| Option | Callback work |
| --- | --- |
| `--consume=ids` | Add emitted entity IDs to an unsigned 64-bit checksum. This is the default. |
| `--consume=all` | Also consume selected coordinates, tags, metadata, references, locations, and member columns. |
| `--consume=count` | Count entities without reading individual output fields. |

Checksums use arithmetic modulo `2^64`.
The full value checksum uses string IDs, not resolved string contents.
Legacy and eager baseline modes support ID and count consumption only.
Compare equivalent callback work when reporting API performance.
Full metadata output includes more fields and wider numeric types than the legacy output.

Examples for Berlin are:

```sh
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf blocks 32 3 --counts=15
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf blocks 32 3 --nodes=7 --consume=all
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf blocks 32 3 --ways=3 --consume=all
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf blocks 32 3 --relations=31 --consume=all
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf blocks 32 3 1
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf entities 32 3 1
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf random-index 32 3 0 1024 1148 --nodes=7
```

## Measure time and memory

The executable writes one CSV row per repetition.

| Field | Meaning |
| --- | --- |
| `setup_seconds` | Reader setup and worker startup before the timed read phase. |
| `index_build_seconds` | Concurrent offset table construction, including worker startup. |
| `read_seconds` | The read phase, excluding explicit reader destruction and memory sampling. |
| `teardown_seconds` | Time after the read phase, including remaining reader destruction. |
| `total_seconds` | Setup, read, and teardown time, excluding explicit memory sampling. |
| `cpu_seconds` | Process user and system CPU time, including memory sampling. |
| `max_rss_kib` | Process peak resident memory. This value accumulates across repetitions in the same process. |
| `live_pss_kib` | Proportional resident memory at the post-read sample. |
| `live_anon_kib` | Anonymous proportional resident memory at that sample. |
| `page_table_kib` | Kernel page-table memory at that sample. |
| `index_bytes` | Total offset-vector capacity across readers. |
| `node_fields`, `way_fields`, `relation_fields` | Effective field masks. A value of `-1` disables the entity type. |
| `count_fields` | Requested count mask. |
| `checksum`, `values_checksum` | Entity ID checksum and selected value checksum. |

Worker threads can release their TLS buffers before the post-read sample.
Thus, the anonymous post-read value does not describe peak decoder storage.
The comparison script also samples `VmRSS`, `RssAnon`, and `VmPTE` every 50 milliseconds while each process runs.
The executable supplies its procfs process ID, so sampling also works across PID namespaces.
These sampled peaks can miss short allocation peaks.
The process peak RSS field does not have that sampling limitation.
Neither measure isolates allocator overhead from decoder allocations.

Random samples keep the readers and their indexes open.
The legacy `input_file()` call releases its reader before post-read sampling.
Compare total time for sequential regression decisions.
Report both index construction and total time for random access.

## Build the baselines

Use the exact `main` revision as the primary baseline.
Use the previous reader branch revision as an additional regression reference.
The older `main` uses Zlib. The reader reference and candidate use libdeflate.
Keep compiler flags and dependency versions equal where those revisions permit it.
Record this decompressor difference with the results.

Extract each revision into a separate build source directory.
Build its static library and dependencies in Release mode.
Use local dependency sources when network access is unavailable.
Compile this benchmark against the matching baseline headers and libraries.

For the `main` API, define `INPUTOSM_BENCH_BASELINE`:

```sh
clang++-19 -std=c++20 -O3 -DNDEBUG -DINPUTOSM_BENCH_BASELINE \
  -Ibuild/deferred-baselines/main-src/include test/benchmark/pbf_benchmark.cpp \
  build/deferred-baselines/main-build/libinputosm.a \
  build/deferred-baselines/main-build/_deps/fmt-build/libfmt.a \
  -lz -lexpat -pthread -o build/deferred-baselines/main-benchmark
```

For the previous reader API, define `INPUTOSM_BENCH_READER_BASELINE`:

```sh
clang++-19 -std=c++20 -O3 -DNDEBUG -DINPUTOSM_BENCH_READER_BASELINE \
  -Ibuild/deferred-baselines/reader-src/include test/benchmark/pbf_benchmark.cpp \
  build/deferred-baselines/reader-build/libinputosm.a \
  build/deferred-baselines/reader-build/_deps/fmt-build/libfmt.a \
  build/deferred-baselines/reader-build/_deps/libdeflate-build/libdeflate.a \
  -lexpat -pthread -o build/deferred-baselines/reader-benchmark
```

The `main` baseline has no random block reader.
Its random access comparison is unavailable.
Use the previous reader revision for scan and index comparisons.

## Run the workload matrix

Use a new output directory for each comparison.
The script copies the binaries before measurement.
It records their hashes, source hashes, revision IDs, input size, commands, and raw results.
It checks counts and ID checksums for equivalent workloads.

```sh
python3 test/benchmark/run_deferred.py \
  --input /mnt/maps/planet-260907.osm.pbf \
  --main build/deferred-baselines/main-benchmark \
  --reader build/deferred-baselines/reader-benchmark \
  --candidate build/test/benchmark/pbf_benchmark \
  --output build/benchmarks/deferred-planet \
  --threads 32 --rounds 3 --requests 4096
```

The matrix contains every nonempty count combination and the requested selective entity workloads.
It includes all-entity decoding with and without metadata, legacy parity, and random scans with and without indexes.
A separately recorded warmup precedes the measured rounds.
The script reverses case order in alternating rounds.
Each measurement runs in a fresh process.
Use `--only TEXT` to select matching case names for a focused comparison.

Run the matrix on an otherwise idle machine.
Do not run builds or other large scans during measurement.
Report medians and measurement ranges.
Do not describe warm-cache measurements as cold-cache results.
