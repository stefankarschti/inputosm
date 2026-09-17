# PBF benchmarks

Build the benchmark with the release configuration.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DINPUTOSM_BENCHMARKS=ON
cmake --build build -j 16
```

The executable writes CSV records to standard output.
Its arguments are:

```text
pbf_benchmark <file> <entities|blocks|random|random-index> <threads> <repetitions> [metadata=0] [requests=256] [max_index=0]
```

| Mode | Operation |
| --- | --- |
| `entities` | Read the complete file through `input_file()`. |
| `blocks` | Open a reader and call `read_blocks()`. |
| `random` | Use separate readers to scan headers for each requested block. |
| `random-index` | Build a table for each reader, then use direct block access. |

Each mode counts all entities and adds their IDs to an unsigned 64-bit checksum.
The checksum must match between equivalent workloads.
Unsigned checksum arithmetic uses modulo `2^64`.

Use `blocks` to get the last data block index before a random benchmark.
The random benchmark requires consecutive data indexes from one through `max_index`.
Check that the reported block count equals `max_index`.
For files with unknown block types, use a separate list of valid indexes instead of this benchmark's request generator.

Both random modes use the same SplitMix64 sequence and seed `0x494e5055544f534d`.
Requests can repeat an index.
The benchmark assigns request numbers to workers in a fixed cyclic order.
Each worker has its own reader and decoder.
The indexed mode builds all reader tables concurrently before the read timer starts.
Each repetition creates new readers and tables.

For Berlin, the development commands are:

```sh
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf entities 32 7
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf blocks 32 7
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf random 32 7 0 1024 1148
build/test/benchmark/pbf_benchmark /mnt/maps/berlin-260514.osm.pbf random-index 32 7 0 1024 1148
```

The `INPUTOSM_BENCH_GENERIC_HEADERS` CMake option supplies the random scan baseline.
It disables the common BlobHeader fast path and uses the general Protocol Buffers parser for every header.
The `INPUTOSM_BENCH_INDEPENDENT_MAPS` option disables sharing between readers of the same file.
Enable both benchmark options to reproduce the initial random access baseline.
Both builds retain the same decoder, validation rules, and benchmark workload.
The default build uses the fast path and falls back to the general parser for other field layouts.
It also shares immutable file mappings between readers.

The `INPUTOSM_BENCH_BASELINE` preprocessor definition builds the benchmark against the previous free block API.
Use the original library and headers for that executable.
This option supports sequential regression measurements against revision `e2458c0`.
It does not add a compatibility API to the new library.

## Measurements

| CSV field | Meaning |
| --- | --- |
| `setup_seconds` | Time before the read phase. Random modes include reader and thread creation. |
| `index_build_seconds` | Elapsed time for concurrent table construction, including worker startup. Zero for other modes. |
| `read_seconds` | Time for the read phase. Random reads exclude table construction and reader destruction. |
| `teardown_seconds` | Time after the read phase and before reader destruction completes. |
| `total_seconds` | Setup, read, and teardown time. Use this field for sequential regression comparisons. |
| `cpu_seconds` | Process user and system CPU time, including memory measurement. |
| `max_rss_kib` | Process maximum resident set size. This is a cumulative high-water value within one process. |
| `index_bytes` | Total allocated offset-vector capacity across all readers. This excludes allocator bookkeeping. |
| `live_rss_kib` | Resident mappings at the measurement point. Shared file pages can appear once for each mapping. |
| `live_pss_kib` | Proportional resident memory from Linux `smaps_rollup`. It apportions shared pages across mappings. |
| `live_anon_kib` | Anonymous proportional resident memory. This includes decoder storage, tables, thread stacks, and retained allocator pages. |
| `page_table_kib` | Kernel page-table storage reported by `VmPTE`. This storage is additional to proportional resident memory. |
| `minor_faults`, `major_faults` | Process page faults during the operation and memory measurement. |

The benchmark samples memory after reading, while explicit reader objects remain open.
The `entities` API releases its temporary reader before that sample.
Worker allocations can remain in the allocator after worker threads finish.
The wall timers exclude the time necessary to read `smaps_rollup`.
The original free block API includes mapping destruction within its read call.
Thus, compare `total_seconds` for old and new sequential APIs.

Run reference and candidate processes separately on an otherwise idle machine.
Record the first pass separately from subsequent warm-cache passes.
Do not claim a cold-cache result unless the cache state supports that claim.
Use repeated measurements and report their spread.
Report index setup and total time with random read time, because table construction affects short workloads.

## Repeated comparison

Use the comparison script to alternate the reference and candidate runs.
The script checks entity counts and ID checksums after each run.
It records executable hashes, commands, raw CSV files, and summary statistics.
Each timed run uses a separate process.
The initial reference block pass warms the file and supplies the maximum block index.

```sh
python3 test/benchmark/run_comparison.py \
  --input /mnt/maps/planet-260907.osm.pbf \
  --reference build/benchmarks/baseline/pbf_benchmark \
  --candidate build/test/benchmark/pbf_benchmark \
  --generic build/generic/test/benchmark/pbf_benchmark \
  --output build/benchmarks/planet-final-v2 \
  --threads 32 --rounds 3 --requests 4096
```

For the random baseline, configure a separate release build.

```sh
cmake -S . -B build/generic \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DINPUTOSM_BENCHMARKS=ON \
  -DINPUTOSM_BENCH_GENERIC_HEADERS=ON \
  -DINPUTOSM_BENCH_INDEPENDENT_MAPS=ON
cmake --build build/generic -j 16
```

For the sequential reference, extract revision `e2458c0` into a separate directory.
Build its library with the same compiler, dependencies, and release flags.
Compile the current benchmark source with `INPUTOSM_BENCH_BASELINE` and the reference headers.
Link that executable with the reference library.

```sh
mkdir -p build/reference-src
git archive e2458c0 | tar -x -C build/reference-src
cmake -S build/reference-src -B build/reference \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 \
  -DBUILD_TESTING=OFF -DINPUTOSM_INTEGRATION_TESTS=OFF
cmake --build build/reference -j 16
clang++-19 -std=c++20 -O3 -DNDEBUG -DINPUTOSM_BENCH_BASELINE \
  -Ibuild/reference-src/include test/benchmark/pbf_benchmark.cpp \
  build/reference/libinputosm.a \
  build/reference/_deps/fmt-build/libfmt.a \
  build/reference/_deps/libdeflate-build/libdeflate.a \
  -lexpat -pthread -o build/reference/pbf_benchmark
```

The [performance report](../../docs/pbf-reader-performance.md) gives the measured results and memory limits.
