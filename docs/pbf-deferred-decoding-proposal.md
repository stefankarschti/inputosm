# PBF blocks with deferred decoding

Status: Implemented in version 0.4.0.

The [performance report](pbf-deferred-performance.md) contains the implementation measurements and correctness results.

Review date: 2026-09-17.
Repository basis: `e49055a`.

This proposal replaces the eager block interface from the [previous reader proposal](pbf-random-access-proposal.md).
It preserves the `input_file()` interface and its nonempty entity batches.
The legacy adapter skips callbacks for empty entity batches.
The method signatures describe the public interface.

## 1. Purpose

Give each block callback an opaque `pbf_block_t` reference.
Defer decompression, string access, entity counts, and entity decoding until the application requests them.
Decode only the selected fields.
Deliver decoded entities in batches that use storage for one primitive group.
Reuse that storage before decoding the next group.

The reader can distribute block callbacks across worker threads.
Every block method runs synchronously in the thread that calls it.
A block method does not start worker threads or submit decoding tasks.

Readers of the same unchanged file continue to share one immutable file mapping.
The existing file block indexes and optional offset tables retain their meanings.

## 2. Design choices

The following requirements are fixed by the request.

| Area | Required behavior |
| --- | --- |
| Block access | Keep block contents private. Decode data only after an explicit request. |
| Header metadata | Supply every requested `HeaderBlock` field through an explicit decode method. |
| Strings | Supply a size query and a string-only callback in table order. Indexes are implicit. |
| String IDs | Return decoded IDs without string table bounds checks. The application checks bounds before lookup. |
| Nodes | Supply a count query and independent ID, latitude, longitude, tag, and metadata options. |
| Ways | Supply a count query and optional tags, node references, metadata, and node locations. |
| Relations | Supply a count query and separate options for tags, member IDs, member types, member roles, and metadata. |
| Storage | Borrow views. Reuse internal `thread_local` buffers. Avoid allocations for individual entities. |
| Execution | Permit concurrent block callbacks. Execute each decode request in its calling thread. |
| Compatibility | Preserve `input_file()`. Implement `input_pbf()` through the new block iteration interface. |
| Performance | Prefer execution speed when additional memory improves execution time. Prevent a sequential scan regression. |

The user selected the following options during design review.

| Question | Selected option | Alternative |
| --- | --- | --- |
| How do batches expose selected fields? | Separate column spans. Unselected columns require no output storage. | Entity records with optional fields. These simplify some consumers but write larger records. |
| Where does header decoding belong? | `reader.decode_header(callback)`. Block iteration continues to supply only data blocks. | Supply opaque header blocks through iteration and `read_block(0)`. This changes the callback population. |
| When does feature validation occur? | During `open()`, as in the current reader. Full header metadata decoding remains explicit. | Validate before the first data read, or require explicit header decoding. |

The following interface choices do not require new configuration options.

| Area | Selected choice | Reason |
| --- | --- | --- |
| Way and relation IDs | Always decode IDs during entity decoding. | Only node IDs need an independent option in the request. Count methods decode no IDs. |
| Member options | Separate flags for IDs, types, and roles. | Applications can select any combination. |
| Entity metadata | Expose all six `Info` fields and their presence. | New types can preserve values that the legacy types cannot represent. |
| Result reporting | Return `bool`. Use output references for counts and parameters. | This follows the reader's current error convention without confusing zero counts with errors. |
| Default node fields | Decode IDs only. | Additional work requires an explicit field selection. |
| Default way and relation fields | Decode IDs only. | Additional lists and metadata require an explicit field selection. |
| Repeated decoding | Decode again with reused storage. | Keeping every decoded column would restore the complete-block memory cost. |
| Cached results | Keep decompressed bytes, descriptors, parameters, and completed counts during the block callback. | These results support subsequent requests without retaining entity arrays. |

## 3. Performance basis

The earlier planet investigation measured the actual counting programs with 32 threads.
The former `count_all` integration program took 23.76 seconds.
The parallel `count_blocks` took 64.48 seconds.
Both programs reported the same entity totals.

The diagnostic decoder processed 903,697 primitive groups in 52,810 data blocks.
Its median data block contained 17 primitive groups.
Berlin contained one primitive group per data block.
Thus, Berlin did not expose the cost of keeping entity arrays across many groups.

These are earlier measurements, not results from this proposal.
The local [counting records](../build/benchmarks/count-investigation/actual-programs.json) contain the program results.
The local diagnostic records compare [group storage](../build/benchmarks/count-investigation/profile-planet-32-1-0.csv) with [complete-block storage](../build/benchmarks/count-investigation/profile-planet-32-0-0.csv).
These build artifacts might not exist in another checkout.

The new counting path must avoid constructing entity arrays.
The full entity path must reuse group storage.
Both changes address the measured storage cost.
Decompression can still limit counting speed.
The benchmark plan does not assume a specific speedup.

## 4. Public reader and opaque block

### Reader operations

| Proposed signature | Contract |
| --- | --- |
| `bool open(const char* filename) noexcept` | Open the shared mapping. Check the initial header structure and required features. |
| `bool decode_header(const pbf_header_handler_t& handler) noexcept` | Decode the header metadata. Call the handler once in the calling thread. |
| `bool read_blocks(const pbf_block_handler_t& handler) noexcept` | Visit every data block from the file start. Use the configured thread count. |
| `bool read_block(size_t index, const pbf_block_handler_t& handler) noexcept` | Visit one data block in the calling thread. Use the existing scan or offset table. |
| `bool build_index() noexcept` | Build the optional file offset table without decoding data payloads. |

The existing lifecycle, move, thread configuration, and index inspection methods remain available.
The reader permits one operation at a time.
Separate readers can operate concurrently.

The block handler retains the signature `bool(const pbf_block_t&)`.
Handler aliases retain the existing `std::function` convention.
Methods borrow handlers by const reference and do not copy them for each block or group.
Application construction of a handler can allocate before the read starts.
The new `pbf_block_t` is a class with private implementation state.
It exposes no entity spans, string table vector, compressed bytes, or Protocol Buffers objects as public data members.
The application cannot construct, copy, or move a block object.
The reader supplies a borrowed reference during the block callback.
Creating that reference does not require a separate heap allocation for each block.

| Proposed block method | Work |
| --- | --- |
| `size_t index() const noexcept` | Return the file block index without accessing the payload. |
| `uint64_t file_offset() const noexcept` | Return the length-prefix offset without accessing the payload. |
| `bool parameters(pbf_parameters_t& result) const noexcept` | Prepare the payload and return its coordinate and timestamp parameters. |
| `bool string_table_size(size_t& result) const noexcept` | Count string entries without copying their bytes or constructing a string-view array. |
| `bool decode_strings(const pbf_string_handler_t& handler) const noexcept` | Visit each string in table order. |
| `bool node_count(size_t& result) const noexcept` | Count ordinary and dense nodes without constructing node records. |
| `bool way_count(size_t& result) const noexcept` | Count way messages without decoding way contents. |
| `bool relation_count(size_t& result) const noexcept` | Count relation messages without decoding relation contents. |
| `bool counts(pbf_counts_t& result) const noexcept` | Get all three counts through one combined traversal. |
| `bool decode_nodes(pbf_node_options_t options, const pbf_node_handler_t& handler) const noexcept` | Decode selected node fields in group batches. |
| `bool decode_ways(pbf_way_options_t options, const pbf_way_handler_t& handler) const noexcept` | Decode selected way fields in group batches. |
| `bool decode_relations(pbf_relation_options_t options, const pbf_relation_handler_t& handler) const noexcept` | Decode selected relation fields in group batches. |
| `bool decode_entities(pbf_entity_options_t options, const pbf_group_handler_t& handler) const noexcept` | Decode selected entity types during one group traversal. |
| `bool validate() const noexcept` | Validate supported entity structures and numeric encodings without entity callbacks. Exclude string table bounds checks. |

These methods are logically const.
Their private state can contain cached descriptors, counters, and reusable buffers.
Logical constness does not permit simultaneous operations on one block.

`read_blocks()` and `read_block()` no longer have a `decode_metadata` argument.
The application selects entity metadata during decoding.
Header metadata has a separate method and type.

The initial header retains file block index zero.
Unknown block types retain their index positions and receive no data callback.
`read_block(0)` and requests for unknown block types return `false` without a callback.
An empty data block still receives one block callback.

### Deferred work

| Trigger | Work before the application callback or result |
| --- | --- |
| `open()` | Map the file. Check the initial header and required features. Retain its descriptor and validation result. |
| Block iteration alone | Read framing and supply an opaque block reference. Do not decompress its data payload. |
| First block payload request | Check the Blob encoding. Decompress if necessary. Locate parameters, string table messages, and primitive groups. |
| First count request | Scan only the structures necessary for the selected count. Cache the completed result. |
| First string request | Scan the string table for the requested operation. Keep the string bytes in place. |
| Entity decode request | Parse selected fields. Fill group storage. Invoke callbacks synchronously. |
| Further payload requests | Reuse decompressed bytes and completed structural information. |
| End of block callback | Release all borrowed block views. Return scratch storage for reuse. |

A compressed block requires complete decompression before any entity count or entity decode can inspect it.
Deferred decoding does not provide partial Zlib decompression or random access within a compressed stream.
Raw Blob payloads borrow bytes from the mapping.
The [PBF file schema](https://github.com/openstreetmap/OSM-binary/blob/master/osmpbf/fileformat.proto) defines these payload forms.

## 5. Header metadata

The proposed header handler has signature `bool(const pbf_header_metadata_t&)`.
`decode_header()` supplies one complete metadata view.
It does not decode any data block.
The view contains the following fields.

| Field | Proposed representation |
| --- | --- |
| `bbox` | Optional `pbf_header_bbox_t` with signed 64-bit `left`, `right`, `top`, and `bottom`. |
| `required_features` | `std::span<const std::string_view>`. Preserve file order. |
| `optional_features` | `std::span<const std::string_view>`. Preserve file order. |
| `writingprogram` | Optional `std::string_view`. |
| `source` | Optional `std::string_view`. |
| `osmosis_replication_timestamp` | Optional `int64_t`, in Unix seconds. |
| `osmosis_replication_sequence_number` | Optional `int64_t`. |
| `osmosis_replication_base_url` | Optional `std::string_view`. |

The optional representations distinguish absence from an empty string or a numeric zero.
Header bounding box coordinates use nanodegrees.
They do not use data block conversion parameters.
The [HeaderBlock schema](https://github.com/openstreetmap/OSM-binary/blob/master/osmpbf/osmformat.proto) defines these fields and units.

`open()` does not construct this metadata view.
It inspects feature declarations before returning success, as selected during review.
This inspection must decompress a compressed header.
It preserves the current header structure checks, including required bounding box member presence.
It does not produce bounding box values, replication values, or other descriptive output.
It does not construct the public feature lists.
It also records whether the header declares `LocationsOnWays`.

This limited inspection is required for safe interpretation of data blocks.
It is the accepted exception to deferring every header field until `decode_header()`.

An application can call `decode_header()` after a successful open, while the reader is idle.
It can request metadata before or after data reads.
An unsupported required feature causes `open()` to fail, as in the current reader.
The proposal does not add a permissive mode for inspecting such files.
Unknown optional features remain visible and do not prevent reading.

The initial supported required features remain `OsmSchema-V0.6` and `DenseNodes`.
`HistoricalInformation` remains unsupported for entity reads in this iteration.
Supporting history files requires a separate behavior decision, including deletion handling through the legacy interface.

Header strings borrow header storage during the header callback.
Feature lists use reusable descriptor vectors.
If a compressed header needs later reuse, the reader can retain its small decompressed payload.
It does not retain application callbacks or metadata objects.

## 6. String table

The proposed string handler has signature `bool(std::string_view value)`.
`decode_strings()` calls it once per entry, including entry zero.
It preserves entry order and repeated string values.
Each call to `decode_strings()` starts at entry zero.
The callback position, starting at zero, gives the implicit string ID.
The callback has no string ID parameter.
Strings cannot arrive out of table order within a block.
The [StringTable schema](https://raw.githubusercontent.com/openstreetmap/OSM-binary/master/osmpbf/osmformat.proto) contains a repeated string-byte field without explicit indexes.
The string IDs belong to the current block only.

`string_table_size()` returns the number of entries, including the empty entry zero.
The first query scans field tags and string lengths.
Subsequent queries return the cached count.
Neither operation requires a vector containing every string view.

The callback receives exact string bytes.
The implementation does not add null terminators or copy string bytes.
Embedded null bytes remain part of the view.
Entry zero must exist and must be empty.

String byte views remain valid until the enclosing block callback returns.
Thus, an application can collect views during string iteration and use them during later entity callbacks for that block.
Those views must not escape the block callback.
The callback's argument object itself need not remain alive after that string callback returns.

Entity decoding returns string IDs for tags, user names, and member roles.
It does not resolve those IDs into strings.
The reader must not compare those IDs with the string table size.
The application must check string table bounds before it uses an ID for lookup.
This rule applies to tag keys, tag values, metadata `user_sid`, and relation member roles.
An ID outside the string table does not cause entity decoding to fail.
Entity decoding does not count or iterate strings merely because selected fields contain string IDs.
It does not require copying strings or constructing a string-view table.
Wire format, numeric representation, and delta overflow checks remain applicable.
Those checks do not establish whether an ID identifies an existing string.

The compatibility adapter can construct a reusable string-view table once per block.
That table permits direct lookup for legacy tag and role strings.

## 7. Entity batch representation

Each typed entity handler has signature `bool(const corresponding_batch_t&)`.
The types are `pbf_node_batch_t`, `pbf_way_batch_t`, and `pbf_relation_batch_t`.
Each batch identifies its file block index, primitive group index, and entity count.
Group indexes start at zero within each data block.
The batch also reports the field selection used for decoding.

Each selected fixed-size column has one entry per entity.
An unselected column has an empty span.
The field selection distinguishes an unselected column from a selected column in an empty batch.
All spans expose const elements.

Variable-size lists use `pbf_list_view_t<T>`.
This view contains a flat value span and a span of unsigned 32-bit offsets.
For `N` entities, a selected list has `N + 1` offsets.
The first offset is zero.
The final offset equals the number of values.
Entity `i` uses the range from `offsets[i]` through `offsets[i + 1]`.
An empty list repeats the preceding offset.
Unselected lists contain no offsets or values.
The decoder checks offset limits before writing output.

This representation avoids allocations and span fixups for individual entities.
It also avoids writing unused scalar fields.
`pbf_tag_ids_t` contains an unsigned 32-bit key ID and value ID.
Every tag list contains these pairs in file order.

The proposed column names make optional output explicit.

| Batch | Output columns |
| --- | --- |
| `pbf_node_batch_t` | `ids`, `raw_latitudes`, `raw_longitudes`, `tags`, `metadata`. |
| `pbf_way_batch_t` | `ids`, `tags`, `node_refs`, `node_locations`, `locations_present`, `metadata`. |
| `pbf_relation_batch_t` | `ids`, `tags`, `member_offsets`, `member_ids`, `member_types`, `member_roles`, `metadata`. |

Entity and member IDs use signed 64-bit spans.
Raw coordinate columns use signed 64-bit spans.
Role columns use unsigned 32-bit spans.
Member type columns use an enum with an unsigned 8-bit underlying type.
Location availability uses unsigned 8-bit values, avoiding `std::vector<bool>` storage.
Metadata columns use spans of `pbf_metadata_t`.

### Node options and output

| Option | Default | Output |
| --- | --- | --- |
| `id` | `true` | Signed 64-bit absolute IDs. |
| `latitude` | `false` | Signed 64-bit raw latitude values after delta decoding. |
| `longitude` | `false` | Signed 64-bit raw longitude values after delta decoding. |
| `tags` | `false` | Lists of `pbf_tag_ids_t`. |
| `metadata` | `false` | A metadata record for each node. |

Each option operates independently.
Latitude decoding does not require longitude decoding or ID output.
The same rule applies to longitude decoding.
Ordinary nodes and dense nodes produce the same public batch type.
Dense columns reset their delta accumulators at the start of each logical dense message.

The batch count remains available when every option is disabled.
The decoder can count the dense ID column without constructing its absolute IDs.
When another selected column requires entity alignment, the decoder checks its item count against the node count.

### Way options and output

Every decoded way supplies its signed 64-bit ID.

| Option | Default | Output |
| --- | --- | --- |
| `tags` | `false` | Lists of `pbf_tag_ids_t`. |
| `node_refs` | `false` | Lists of signed 64-bit absolute node IDs. |
| `metadata` | `false` | A metadata record for each way. |
| `node_locations` | `false` | Lists of raw latitude and longitude pairs, with location availability for each way. |

Location decoding operates independently of reference output.
If locations are selected, the decoder checks the reference count without constructing unselected reference values.
For each way with locations, reference, latitude, and longitude counts must agree.
The decoder resets each location accumulator at the start of the way.
It checks the `LocationsOnWays` declaration when location fields are present.
The [Way schema](https://github.com/openstreetmap/OSM-binary/blob/master/osmpbf/osmformat.proto) specifies these parallel arrays and the feature declaration.

If a way has no location fields, its location list is empty and its availability flag is false.
The decoder does not fetch node coordinates from another block or file.
An empty way can have an empty location list.
Availability describes encoded field presence, not whether the list contains points.
Selecting locations does not make them mandatory for every way.

### Relation options and output

Every decoded relation supplies its signed 64-bit ID.

| Option | Default | Output |
| --- | --- | --- |
| `tags` | `false` | Lists of `pbf_tag_ids_t`. |
| `member_ids` | `false` | Signed 64-bit absolute member IDs. |
| `member_types` | `false` | Member types: node, way, or relation. |
| `member_roles` | `false` | Unsigned 32-bit role string IDs. |
| `metadata` | `false` | A metadata record for each relation. |

The three selected member columns use one shared member-offset span.
They preserve member order.
If any member column is selected, the decoder checks all three encoded column lengths.
It can count unselected columns without accumulating their deltas or storing their values.
When no member column is selected, it skips the member payloads.

Role output does not require member ID or type output.
Selected roles return decoded string IDs without checking string table bounds.
Selected types reject values outside the three supported member types.
Selected IDs check delta arithmetic for overflow.

### Entity metadata and conversion parameters

`pbf_metadata_t` contains a presence mask and the following fields.

| Field | Type | Value when absent |
| --- | --- | --- |
| `version` | `int32_t` | `-1`. |
| `raw_timestamp` | `int64_t` | Zero, with the presence bit clear. |
| `changeset` | `int64_t` | Zero, with the presence bit clear. |
| `uid` | `int32_t` | Zero, with the presence bit clear. |
| `user_sid` | `uint32_t` | Zero, with the presence bit clear. |
| `visible` | Boolean value | True, with the presence bit clear. |

The presence mask distinguishes absent values from encoded defaults.
The decoder normalizes ordinary `Info` and dense metadata into this representation.
Selected dense metadata columns must match the entity count when present.
Absent metadata does not cause a decode error.
The [Info and DenseInfo definitions](https://github.com/openstreetmap/OSM-binary/blob/master/osmpbf/osmformat.proto) define these source fields.

`pbf_parameters_t` contains `granularity`, `lat_offset`, `lon_offset`, and `date_granularity`.
Defaults remain 100, 0, 0, and 1000, respectively.
Entity batches supply these parameters with their raw values.

| Conversion | Formula |
| --- | --- |
| Latitude in nanodegrees | `lat_offset + granularity * raw_latitude` |
| Longitude in nanodegrees | `lon_offset + granularity * raw_longitude` |
| Timestamp in Unix milliseconds | `date_granularity * raw_timestamp` |

The new interface does not silently narrow values or convert them to floating point.
Applications must use arithmetic that can represent the converted result.
The legacy interface retains its existing raw coordinate and timestamp meanings.

## 8. Counts and validation cost

No file framing field gives the number of nodes, ways, or relations in a data block.
The first count request must inspect the decompressed payload.

| Count | First-query work after decompression | Cached-query work |
| --- | --- | --- |
| Strings | Scan string field tags and lengths. Skip string bytes. | Constant time. |
| Ordinary nodes | Count node message occurrences in primitive groups. Skip message bodies. | Constant time. |
| Dense nodes | Count values in the ID column. Do not add deltas or construct nodes. | Constant time. |
| Ways | Count way message occurrences. Skip message bodies. | Constant time. |
| Relations | Count relation message occurrences. Skip message bodies. | Constant time. |

The dense counter scans varint boundaries and rejects truncated or oversized varints in the scanned ID stream.
It supports packed fields, unpacked fields, and repeated packed segments.
It does not assume one byte per ID delta.
The counter processes eight-byte ranges when both ends coincide with varint boundaries.
It uses the general varint decoder for the remaining ranges.

`counts()` combines traversal for all three entity counts.
Separate count methods can reuse completed group classifications and counts.
Entity decoding can also complete a count cache when it visits all relevant groups.
Cancellation must not mark a partial count as complete.
Repeated count calls do not decode the block again.

A fast count is not full entity validation.
For example, a node count can succeed when an unrequested tag field is invalid.
A framing-only block read can succeed without examining an invalid compressed payload.
This behavior follows from deferred decoding and must be explicit in the API documentation.

All operations check byte bounds and the wire structures they traverse.
Entity methods validate selected fields and the structural dependencies necessary to align them.
They can skip unrelated field contents.
`validate()` checks supported entity structures and numeric encodings, including unselected fields, without application entity callbacks.
Neither `validate()` nor entity decoding checks string IDs against the string table size.
The application retains responsibility for those bounds checks.
Unsupported changeset groups and mixed entity kinds remain errors when their groups are inspected.

The parser must preserve the existing supported Protocol Buffers forms.
These include reordered fields, unknown fields, repeated packed segments, and valid unpacked numeric fields.
Repeated singular submessages follow merge semantics where applicable.
Counts and decoding must agree on logical dense messages after those merges.

## 9. Batch order and combined decoding

`decode_nodes()`, `decode_ways()`, and `decode_relations()` visit matching groups in file order within the current block.
Each method invokes its handler once per matching primitive group in the first implementation.
An empty matching group can produce an empty batch.
An empty block with no groups produces no entity callbacks.
Entity order within each group remains unchanged.

`decode_entities()` accepts selections for all three entity types.
`pbf_entity_options_t` contains an optional options value for each entity type.
An absent options value disables that entity type.
Default construction disables all three types.
Its handler receives `pbf_group_batch_t` once per primitive group.
This batch identifies the group kind and contains the selected typed batches.
The group kind distinguishes ordinary nodes, dense nodes, ways, relations, and empty groups.
Disabled entity types contain no output columns.

This combined method permits one traversal when an application needs several entity types.
Calling the three individual methods is also valid.
Those separate calls can repeat group inspection, but they reuse the decompressed block.

The decoder fills output only for the current group.
It invokes the handler before reusing those buffers for the next group.
It does not combine all groups into complete-block entity arrays.
It does not retain decoded columns between separate decode requests.

## 10. Thread execution and storage lifetime

| Operation | Execution thread |
| --- | --- |
| `decode_header()` | Its caller. |
| `read_block()` callback | Its caller, regardless of the reader thread setting. |
| `read_blocks()` with one thread | Its caller. |
| `read_blocks()` with multiple threads | The worker assigned to each block. Callbacks can overlap and arrive out of file order. |
| Any block method | The thread that calls that method. |
| Entity or string callback | The thread that called its decode method. |

The application must call block methods from the active block callback's thread.
It must not transfer the borrowed block reference to another thread.
Separate readers can provide concurrent random access on separate threads.
The reader joins its workers before a read method returns.

| Object or view | Valid lifetime |
| --- | --- |
| Opaque block reference | Its block callback only. |
| Header metadata and its strings | Its header callback only. |
| String bytes from `decode_strings()` | The enclosing block callback. |
| Entity batch spans, list offsets, tags, references, members, and metadata | Their entity callback only. |
| Copied counts, indexes, parameters, and scalar values | Owned by the application after copying. |

The string lifetime permits useful lookup tables without string copies.
Entity buffers have the shorter lifetime because the decoder reuses them for each group.
Retaining a span object does not extend the lifetime of its referenced storage.

Block operations are sequential and non-reentrant on the same block.
Only `index()` and `file_offset()` can be called during an active entity or string callback.
Applications should obtain counts, parameters, and string views before starting entity decoding when those callbacks need them.
Nested operations on the same reader are invalid.
Nested reads through a different reader are permitted and require separate scratch frames.

### Internal storage

Use a `thread_local` pool of reusable scratch frames.
Each active decoding context holds an exclusive frame associated with its reader and block identity.
A nested read through a different reader acquires another frame.
It cannot clear or resize storage used by the outer callback.

Do not use a permanent table entry for every reader address.
Such a table can retain unused buffers and confuse reused object addresses.
An inactive frame contains reusable capacity but no active mapping references or borrowed views.
The pool size follows maximum simultaneous nesting in that thread, not the number of readers previously opened.

Each frame can retain a decompressor, decompression buffer, group descriptors, count caches, and selected output vectors.
It can also retain optional string lookup and legacy output vectors.
State resets between blocks while vector capacity remains available.
Allocation can occur on first use or when a larger group exceeds retained capacity.
Steady-state decoding must not allocate for each entity or callback.

Frames retain the decompressed block until its outer block callback returns.
Entity vectors need capacity for the largest processed group, not the total entities across all groups.
The reader's bounded queue contains compressed block descriptors only.
It does not retain decompressed payloads for queued blocks.

Worker thread exit releases its TLS storage.
Long-lived application threads can retain capacity after readers close.
The benchmark must report this retained memory separately from index storage and mapped file pages.
An explicit scratch-release API is outside this iteration unless measurements show a practical need.

## 11. Errors and cancellation

Methods return `true` only when their requested operation completes and all invoked handlers return `true`.
They return `false` on malformed selected data, unsupported required features, allocation failure, handler failure, or handler exceptions.
They catch exceptions before returning from the public `noexcept` boundary.
Count and parameter output arguments remain unchanged on failure.

A nested decoding failure marks the enclosing read operation as failed.
The outer block handler cannot hide that failure by returning `true`.
A callback result of `false` requests cancellation of the enclosing read.
It is not a successful way to truncate one entity batch sequence.

After cancellation, the implementation stops scheduling work and checks cancellation between groups.
Callbacks already executing on other workers can finish.
The reader joins every worker before returning `false`.
Earlier callbacks can have observable application effects before a later error occurs.
The API does not provide transactional callback delivery.

Errors use the existing log facility.
Diagnostics identify the file block index and offset when available.
A failed operation releases its active scratch state.
The reader remains available for a later independent operation on the unchanged file.

## 12. `input_file()` compatibility

The `input_file()` signature and the existing entity structures remain unchanged.
The XML path remains unchanged.
The legacy global thread setting and parser context values retain their meanings.
The legacy API does not gain permission for simultaneous top-level input operations.

The PBF call path becomes:

`input_file()` → `input_pbf()` → public `pbf_reader_t::read_blocks(handler)` → deferred group decoder → legacy entity handlers.

`input_pbf()` constructs a reader and copies the existing thread setting.
Its block callback explicitly requests the legacy entity field selection.
The reader itself does not select eager decoding for this caller.
The current private `read_blocks(..., group_batches)` overload is removed during implementation.

The shared decoder supports output writers for column batches and legacy entity records.
The legacy writer fills `node_t`, `way_t`, and `relation_t` directly in reusable group buffers.
It resolves tags and member roles through a reusable string-view table.
As a consumer of string IDs, the adapter checks bounds before string lookup.
It also preserves the existing metadata string-ID checks when legacy metadata decoding is enabled.
These checks belong to the compatibility adapter, not the new reader's entity decoding methods.
It does not first construct column batches and copy them into legacy records.

The public block iteration path supplies the same opaque context to both consumers.
An internal adapter can access the shared decoder through that context.
It cannot bypass block iteration, create another mapping, or use a separate parsing engine.

The adapter preserves the following order for each primitive group.

| Order | Callback condition |
| --- | --- |
| 1 | Call the supplied node handler only when the decoded node batch is nonempty. |
| 2 | Call the supplied way handler only when the decoded way batch is nonempty. |
| 3 | Call the supplied relation handler only when the decoded relation batch is nonempty. |

Empty groups, including empty dense-node groups, cause no legacy entity callbacks.
A block with no primitive groups has no entity callbacks.
A failed callback prevents subsequent callbacks for that group.
The adapter preserves block indexes, thread indexes, and callback context restoration.

Legacy coordinates and timestamps remain raw values.
Legacy metadata remains limited to the existing version, timestamp, and changeset fields.
Absent legacy metadata remains zero.
When metadata decoding is enabled, existing narrowing checks remain in force.
The new wider metadata types do not silently change legacy results or accepted numeric ranges.

The adapter preserves existing validation when handlers are absent.
It must not skip an entity type merely because its handler is empty.
With all handlers empty, `input_file()` must still validate according to its current metadata setting.
The shared decoder uses an internal legacy validation policy for this behavior.
That policy preserves current treatment of optional fields that the old interface ignores.

## 13. Migration of direct block consumers

This iteration changes the direct PBF block API.
The existing eager `pbf_block_t` fields are replaced by methods.
The reader methods lose their metadata Boolean parameter.
Direct block consumers require source changes and a rebuild.
The proposal does not retain an eager compatibility wrapper.

| Existing use | Proposed use |
| --- | --- |
| `block.index` and `block.file_offset` | `block.index()` and `block.file_offset()`. |
| Entity span sizes | `counts()` or the individual count methods. |
| Complete string table span | `string_table_size()` and `decode_strings()`. |
| Node span | `decode_nodes()` with an explicit field selection. |
| Way span | `decode_ways()` with an explicit field selection. |
| Relation span | `decode_relations()` with an explicit field selection. |
| All entity spans | `decode_entities()` for one traversal and group callbacks. |
| Public conversion fields | `parameters()` or the parameters supplied with entity batches. |
| `read_blocks(decode_metadata, handler)` | `read_blocks(handler)`, followed by explicit decoding inside the handler. |

The integration program `count_blocks` counts data block callbacks through `read_blocks()`.
It does not request payload decompression or entity counts.
The random block integration program will select the fields that it prints or checks.
The integration program `count_entity` uses `counts()` to count nodes, ways, and relations without entity arrays.
The benchmark driver retains its `entities` mode for compatibility measurements through `input_file()`.

## 14. Implementation and test plan

The implementation follows the reviewed design.
The steps below define the implementation and verification sequence.

1. Freeze the current release binaries and benchmark commands as references.
2. Add opaque block contexts and the public deferred iteration signatures.
3. Separate header inspection, payload preparation, counts, and entity decoding.
4. Add header metadata and string iteration methods.
5. Add selective column writers and group callbacks.
6. Add the legacy writer to the same decoder traversal.
7. Replace the private legacy iteration overload with the public block iteration path.
8. Update direct block consumers and documentation.
9. Run correctness, lifetime, concurrency, and performance checks.

| Test area | Required cases |
| --- | --- |
| Deferred work | A framing-only callback does not decompress a data block. The first payload request does. Later requests reuse it. |
| Header | Every requested field; all fields absent; empty strings; zero replication values; signed 64-bit limits; compressed and raw headers. |
| Header validity | Missing bounding box members; merged submessages; unsupported required features; unknown optional features; request order before and after reads. |
| Strings | String-only callback signature; implicit indexes; entry zero; repeated entries; embedded null bytes; invalid lengths; ordered callbacks; cached size; borrowed lifetime. |
| String IDs | Return IDs outside the table for tags, user names, and roles. Do not count strings or check table bounds implicitly. |
| Counts | Ordinary and dense nodes; empty groups; many groups; mixed entity blocks; packed and unpacked values; split packed segments. |
| Selective nodes | All 32 option combinations; independent coordinates; delta resets; absent metadata; callbacks with no selected columns. |
| Selective ways | All 16 option combinations; absent locations; locations without reference output; array size errors; missing feature declaration. |
| Selective relations | All 32 option combinations; independent member columns; unchecked role string IDs; invalid types; unequal lengths; delta overflow. |
| Metadata | All six fields; presence masks; ordinary and dense forms; large timestamps and changesets; user indexes; defaults. |
| Optional work | Unselected fields produce no output storage. Selected structural dependencies receive the required checks. |
| Batch lifetime | Group buffers can be reused after callbacks. String bytes survive until the enclosing block callback returns. |
| Threading | Decode callbacks keep the calling thread ID. Separate readers share a mapping and hold independent scratch frames. |
| Nested reads | A different reader can decode within a callback without changing outer views. Same-reader recursion fails. |
| Failure | Exceptions; callback cancellation; ignored nested failure; unchanged count outputs on failure; reader reuse after failure. |
| Random access | Scan and offset-table results agree. Header and unknown indexes retain their behavior. Offsets beyond 4 GiB work. |
| Legacy behavior | Skip empty batches. Preserve entity values, nonempty callback order, metadata settings, string lookup checks, parser context, validation, and XML behavior. |
| Storage | Allocation counts stabilize after warmup. Closing readers releases mapping references even when TLS capacity remains. |

Use existing fixtures as the compatibility basis.
Add focused fixtures for header fields, way locations, selected columns, and nested reader lifetimes.
Use sanitizers for memory, undefined behavior, and thread checks where supported.
Do not use real-map counts alone to prove callback compatibility.

## 15. Benchmark and acceptance plan

Use Berlin during implementation checks.
Use the planet file for final sequential and random access benchmarks with 32 threads.
Add 1-thread and 16-thread runs when they help identify scaling limits.
Keep those diagnostic runs separate from the required 32-thread comparison.

| Workload | Reference and purpose |
| --- | --- |
| `input_file()` with legacy entity callbacks | Compare against the frozen current implementation. Reject a repeatable sequential regression. |
| `count_entity` using `counts()` | Compare against eager and legacy entity counting. Measure the benefit of avoiding entity arrays. |
| `count_blocks` using framing-only block iteration | Measure descriptor and scheduling cost without payload requests. Do not compare it as equivalent entity work. |
| Node IDs only | Measure selected decoding with a checksum over every emitted ID. |
| Coordinates only | Measure independent coordinate selection and compare coordinate checksums. |
| All legacy-equivalent fields | Compare group output against equivalent values from the legacy interface. |
| Strings, tags, roles, and metadata | Check selective costs and verify field-specific checksums. |
| Way references and locations | Use a fixture with `LocationsOnWays` if the maps do not contain these fields. |
| Random access without an index | Scan framing from the file start for each request. Decode the selected block workload. |
| Random access with an index | Use the same request sequence and decoded fields. Include index construction in total time. |

Run references and candidates as separate processes in alternating order.
Use at least three measured planet rounds after a separately recorded warmup.
Keep compiler, dependencies, optimization flags, input files, and callback work identical for each equivalent comparison.
Record commands, revisions, executable hashes, raw results, medians, and measurement ranges.
Use the same deterministic 4,096-request sequence for both random modes.
Use separate readers on 32 calling threads with one shared mapping.

Preserve the existing scan and index implementations for the first deferred-decoding comparison.
This isolates the effect of deferred decoding from changes to block lookup.

Measure first-use costs and repeated reads separately.
Report setup, feature validation, index construction, read time, and teardown time.
Report total elapsed time for sequential regression decisions.
Do not treat a framing-only read as a faster replacement for a validated entity scan.
Do not mix span-size counting results with benchmarks that checksum every entity.

Report memory as well as time.
Include peak RSS, sampled PSS, anonymous memory, page-table memory, and allocated index capacity.
Also report decompression capacity, entity buffer capacity, string descriptor capacity, and retained TLS capacity.
Sample memory while readers and worker buffers remain active, then sample again after reader closure.
Record allocation counts after warmup with a separate diagnostic build if instrumentation changes timing.
Confirm that 32 readers do not create 32 file mappings.

Correctness requires matching counts and checksums for equivalent field selections.
Compatibility requires matching legacy callback behavior on the focused fixtures.
Performance acceptance requires no repeatable `input_file()` regression outside the measured run variation.
If the new compatibility path regresses, improve that path before implementation is considered complete.
The count path should outperform eager complete-block decoding on the planet file.
Whether it surpasses legacy entity counting must be established by measurement.

## 16. Scope limits

This iteration does not add entity-level random access, persistent index files, or asynchronous entity decoding.
It does not keep decoded entities after callbacks.
It does not add new compression formats or history-file support.
The implementation can add internal fast paths only when they preserve the selected decoding and validation contracts.
