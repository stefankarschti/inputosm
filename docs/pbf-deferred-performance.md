# Deferred PBF decoding measurements

Measurement date: 2026-09-17.

The new reader delays payload decompression and entity decoding until the application requests data.
Count methods do not construct entity arrays.
Entity methods write selected columns into reusable storage for one primitive group.
The compatibility adapter writes legacy records directly through the public block iteration API.

## Planet results

The file contains 52,810 data blocks, 10,846,489,004 nodes, 1,216,465,009 ways, and 14,708,384 relations.
Complete entity scans produced the same ID checksum, `5330885097698811996`, with arithmetic modulo `2^64`.

### Sequential compatibility and complete entity decoding

| Interface | Metadata | Median seconds | Range | Anonymous peak MiB | Peak RSS GiB |
| --- | --- | ---: | --- | ---: | ---: |
| `main` legacy | No | 34.622 | 34.431–34.627 | 810.5 | 89.026 |
| Previous reader legacy | No | 24.426 | 24.339–24.484 | 876.2 | 89.087 |
| New reader legacy | No | 24.666 | 24.658–24.720 | 887.3 | 89.094 |
| New columns | No | 22.116 | 22.074–22.185 | 623.5 | 88.838 |
| `main` legacy | Yes | 38.875 | 38.797–38.880 | 811.8 | 89.022 |
| Previous reader legacy | Yes | 28.020 | 27.927–28.035 | 865.7 | 89.077 |
| New reader legacy | Yes | 28.275 | 28.242–28.306 | 889.6 | 89.101 |
| New columns | Yes | 27.870 | 27.838–27.913 | 650.8 | 88.871 |

The compatibility API reduces elapsed time against `main` by 28.8% without metadata.
With metadata, the reduction is 27.3%.
The requested comparison with `main` has no sequential regression.

The additional comparison shows a small regression against the previous libdeflate reader.
The compatibility API takes 0.98% longer without metadata and 0.91% longer with metadata.
The measurement ranges do not overlap, so this report does not dismiss the difference as noise.
This small compatibility cost remains in the implementation.

New column decoding takes 9.5% less time than the previous legacy reader without metadata.
Its anonymous peak is 623.5 MiB, compared with 876.2 MiB.
With metadata, new columns take 0.5% less time, while exposing all six metadata fields.

### Counts

`B` means data blocks, `N` means nodes, `W` means ways, and `R` means relations.
Each row uses the stated `--counts` mask.

| Mask | Counts | Median seconds | Range | Anonymous peak MiB |
| ---: | --- | ---: | --- | ---: |
| 1 | B | 0.395 | 0.390–0.405 | 0.2 |
| 2 | N | 12.715 | 12.690–12.792 | 369.9 |
| 3 | B+N | 12.780 | 12.748–12.938 | 373.1 |
| 4 | W | 12.605 | 12.594–12.609 | 373.8 |
| 5 | B+W | 12.586 | 12.585–12.592 | 377.2 |
| 6 | N+W | 13.104 | 13.086–13.177 | 348.3 |
| 7 | B+N+W | 13.081 | 13.080–13.087 | 364.6 |
| 8 | R | 12.576 | 12.573–12.581 | 369.6 |
| 9 | B+R | 12.560 | 12.538–12.566 | 358.7 |
| 10 | N+R | 13.086 | 13.084–13.128 | 364.6 |
| 11 | B+N+R | 13.111 | 13.075–13.133 | 369.1 |
| 12 | W+R | 12.924 | 12.912–12.954 | 368.2 |
| 13 | B+W+R | 12.924 | 12.920–12.929 | 375.7 |
| 14 | N+W+R | 12.728 | 12.725–12.758 | 363.9 |
| 15 | B+N+W+R | 12.728 | 12.719–12.753 | 359.9 |

The eager baselines decode complete blocks before counting their entity spans.
They do not sum entity IDs in this comparison.

| Eager baseline | Median seconds | Range | Anonymous peak MiB | Peak RSS GiB |
| --- | ---: | --- | ---: | ---: |
| `main` | 104.044 | 103.996–104.072 | 3515.9 | 91.666 |
| Previous reader | 64.452 | 64.415–64.470 | 3424.9 | 91.581 |

All-count throughput improves by a factor of 8.17 against `main` eager counting.
The factor is 5.06 against the previous reader.
The anonymous peak falls from 3424.9 MiB to 359.9 MiB.
The block-only count does not decompress or validate data payloads.

### Selective entity decoding

These callbacks consume every selected value, including metadata presence bits.
Way and relation IDs remain selected in addition to the stated optional fields.

| Selection | Flags | Median seconds | Range | Anonymous peak MiB |
| --- | --- | ---: | --- | ---: |
| Node IDs and positions | `--nodes=7 --consume=all` | 16.008 | 15.963–16.052 | 373.3 |
| Way tags and node references | `--ways=3 --consume=all` | 16.870 | 16.860–16.891 | 563.0 |
| All relation fields | `--relations=31 --consume=all` | 12.707 | 12.688–12.735 | 371.8 |

Each selective workload still decompresses every data block to identify its entity messages.
Thus, the relation-only scan retains much of the common decompression cost.

### Random block access

Each workload requests 4,096 blocks with the same deterministic request sequence and 32 workers.
Repeated requests can select the same block.
Full decoding selects all legacy-equivalent fields without metadata and consumes entity IDs.
The count workloads request all entity counts.

| Reader and selection | Index | Total seconds | Range | Read seconds | Index build seconds | Anonymous peak MiB | Peak RSS GiB |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: |
| Previous reader, full | No | 7.083 | 7.067–7.133 | 6.577 | 0.000 | 2917.7 | 12.405 |
| New reader, full | No | 2.348 | 2.303–2.355 | 1.972 | 0.000 | 496.2 | 9.780 |
| New reader, counts | No | 1.589 | 1.583–1.608 | 1.214 | 0.000 | 408.0 | 9.679 |
| Previous reader, full | Yes | 7.144 | 7.127–7.324 | 6.395 | 0.260 | 2841.5 | 12.331 |
| New reader, full | Yes | 2.172 | 2.136–2.201 | 1.535 | 0.259 | 477.7 | 9.878 |
| New reader, counts | Yes | 1.431 | 1.421–1.480 | 0.792 | 0.261 | 346.9 | 9.735 |

Full random access improves by a factor of 3.02 without an index.
Indexed access improves by a factor of 3.29.
Both values include setup and teardown.

For the new reader, indexing reduces the full-decode read phase by 22.1%.
Including index construction and teardown, the reduction is 7.5%.
For random counts, the total reduction is 9.9%.
The index uses 16 MiB across 32 readers.
Index construction occurs concurrently and takes approximately 0.26 seconds.

The new random workloads use approximately 173 MiB of page-table memory per process.
The offset table does not create additional file mappings.
The measured anonymous peaks vary with group sizes and worker assignment.
Consequently, subtracting anonymous peaks does not isolate the 16 MiB index allocation.

Full random decoding produces 844,176,000 nodes, 93,752,000 ways, and 1,184,114 relations, including repeated requests.
The ID checksum is `6154689938499644976` in both reader versions and both access modes.
The `main` API has no random block reader, so it cannot supply this baseline.

## Berlin development comparison

The same binaries also complete the 102-measurement matrix on Berlin.
These small-file times include thread startup and teardown.

| Workload | `main` seconds | Previous reader seconds | New reader seconds |
| --- | ---: | ---: | ---: |
| Legacy, no metadata | 0.0720 | 0.0581 | 0.0585 |
| Legacy, metadata | 0.0745 | 0.0596 | 0.0616 |
| All entity counts | 0.0705 | 0.0564 | 0.0206 |
| Random full decode | Unavailable | 0.1157 | 0.0977 |
| Indexed random full decode | Unavailable | 0.1328 | 0.0947 |

Many Berlin runs finish before the 50-millisecond memory sampler obtains a sample.
Their missing sampled-memory results use `null` in the archived summary.
The planet memory results provide the useful storage comparison.

## Correctness checks

All nine Release CTest checks pass: eight unit checks and one integration check.
All nine checks also pass with AddressSanitizer and UndefinedBehaviorSanitizer.
The sanitizer command uses `ASAN_OPTIONS=detect_leaks=0`; it does not check memory leaks.
Seven unit checks pass with ThreadSanitizer.
The allocation check is skipped under ThreadSanitizer because both supply allocation operators.

The new fixtures cover all 32 node, 16 way, and 32 relation option combinations with raw and compressed payloads.
They check header fields, metadata presence, wide integers, way locations, and string order.
They also check counts, cached results, numeric errors, cancellation, nested readers, and callback thread execution.
String IDs outside the string table remain available without reader bounds checks.
Existing entity-value checks use a test adapter that collects the borrowed column batches.
Legacy callback and XML checks remain active.

The allocation check records no C++ heap allocations during 100 repeated reads after buffer preparation.
This fixture requests counts, strings, entity columns, and header metadata.
It does not claim that file opening, thread creation, or initial buffer growth requires no allocations.

Integration checks now verify latitude buckets and ferry totals in addition to existing formatted output.
The Berlin ferry example reports 21 ferries and 188 unique referenced nodes.
The benchmark completes 102 measurements on Berlin and 102 measurements on the planet file without count or checksum failures.

## Result records

The [planet summary](benchmarks/deferred/planet/summary.json) contains medians and ranges for every metric.
The [planet raw records](benchmarks/deferred/planet/results.json) contain all 102 commands, timings, checksums, and memory samples.
The [planet manifest](benchmarks/deferred/planet/manifest.json) records binary hashes, source hashes, revisions, and input identity.
The [machine record](benchmarks/deferred/planet/environment.json) contains compiler, processor, memory, and dependency details.
The [Berlin summary](benchmarks/deferred/berlin/summary.json) and [raw records](benchmarks/deferred/berlin/results.json) retain the development comparison.

## Measurement method

The primary baseline is `main` at `f6973c3`.
The additional reader baseline is `e49055a`.
Both baselines use their original library source and the same benchmark driver.
The primary baseline uses Zlib 1.3.
The reader baseline and candidate use libdeflate 1.26.
Thus, comparisons with `main` include the earlier decompressor improvement.
Comparisons with the reader baseline isolate the subsequent reader changes more closely.

The machine has an AMD Ryzen 9 7950X3D processor with 16 cores and 32 hardware threads.
All measurements use 32 threads.
Clang 19.1.1 builds each executable with C++20, `-O3`, and `-DNDEBUG`.
All builds use fmt 12.2.0.
No builds or other library scans run during the measured workloads.

The planet file is `/mnt/maps/planet-260907.osm.pbf`.
Its size is 94,746,421,723 bytes.
Berlin development checks use `/mnt/maps/berlin-260514.osm.pbf`, which contains 97,937,692 bytes.

Each workload runs in a new process for each of three rounds.
A complete candidate scan precedes the measured rounds.
The second round reverses the workload order.
These are warm-cache measurements.
They do not measure cold storage throughput.

Tables report median elapsed seconds and minimum-to-maximum ranges.
Total time includes reader setup, worker startup, index construction when selected, reading, and reader teardown.
The benchmark excludes its own post-read memory inspection from elapsed time.
Sequential reader teardown includes file unmapping, which takes approximately three seconds for this planet file.
The raw records also contain separate setup, read, index construction, and teardown times.

The driver checks counts and ID checksums against the complete scan.
It checks repeated selective checksums and random-access parity between reader versions.
The selective workloads consume every selected column value.
The full-entity comparison consumes IDs after the decoder constructs all requested fields.
New columns return string IDs; legacy records return resolved strings.
The new metadata view also contains more fields and wider integers than the legacy view.
Therefore, the output representations differ even when entity totals and IDs agree.

## Memory interpretation

The tables distinguish process RSS from anonymous memory.
Peak RSS includes resident pages from the file mapping.
It does not represent private decoder storage alone.
All readers of the same file share one mapping within each process.
The random-access tests use 32 reader objects and one mapping.

The runner samples anonymous memory and page-table memory every 50 milliseconds.
These samples can miss brief peaks.
They include allocator overhead and other process allocations.
The executable also records the operating system's process peak RSS.
Worker exit can release thread-local buffers before the post-read memory sample.
Use the sampled anonymous peak when comparing decoder storage.

Each indexed reader owns its offset table.
For this planet file, each table allocates 512 KiB.
The 32-reader indexed workload allocates 16 MiB for offset tables.
The scan workload allocates no offset table.

## Why counting becomes faster

The eager block API retains decoded entities for the complete block before its callback.
The planet file contains 903,697 primitive groups in 52,810 data blocks.
The median data block contains 17 primitive groups.
This storage requirement explains why eager block counting takes longer than the legacy group callbacks.
Berlin has one primitive group per data block and does not expose the same storage cost.

The new block count reads file framing only.
Entity counts require payload decompression, but they do not decode entity columns or resolve strings.
Dense node counting examines varint boundaries in eight-byte ranges when possible.
The checked scalar path handles remaining boundaries and long varints.

The combined `counts()` method examines each group once.
Two individual count methods examine the groups twice.
Consequently, a two-count combination can take slightly longer than the combined three-count query.
Decompression remains the main common cost for these count workloads.

Full entity decoding uses reusable group buffers.
The dense metadata path reads packed columns together and writes each metadata record once.
This avoids separate writes to the same records for each metadata field.
No string table lookup occurs during new entity decoding.
Applications must check string ID bounds before lookup.

## Reproduce the measurements

The [benchmark guide](../test/benchmark/README.md) gives build commands and the complete field mask definitions.
The driver accepts any combination of node, way, relation, string, and count selections.
The comparison script measures every nonempty count combination and the requested entity workloads.
It also compares random scans with indexed access.

Use a new output directory for each run.
Keep the machine free of other large workloads during measurement.
Use the same file, thread count, request sequence, compiler flags, and dependency versions for each comparison.

Framing-only counting does not validate compressed payloads.
Selective decoding does not validate unrequested entity fields.
These operations perform less work than a complete legacy decode.
