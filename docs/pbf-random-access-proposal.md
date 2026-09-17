# PBF reader with sequential and indexed block access

Status: Implemented in version 0.3.0.

The [deferred decoding proposal](pbf-deferred-decoding-proposal.md) describes the next design iteration.
That iteration changes the block interface and preserves `input_file()`.

Review basis before implementation: Repository revision `e2458c0`.

## Purpose

Move the `input_pbf_blocks()` API into `pbf_reader_t` as `read_blocks()`.
Add `read_block()` to read one PBF data block by its existing `pbf_block_t::index` value.
Implement `input_pbf()` through a temporary `pbf_reader_t` and its `read_blocks()` method.
Its internal batch handler passes primitive-group spans to the node, way, and relation handlers.
An application can keep an index from a sequential read and use it for a subsequent random read.
The file contents must remain the same between these operations.

The reader keeps a file mapping open between calls to either method.
Each `read_blocks()` call scans the file once and delivers all data blocks unless cancellation or an error stops the operation.
Without an offset table, each `read_block()` call scans file block headers from the start to the selected index.
It skips earlier Blob payloads without decoding them.
The reader decodes the selected data block and calls the existing block handler.

Separate reader objects can run either method simultaneously.
Each reader uses its own decoder storage and thread configuration.
`read_block()` runs its callback in the calling thread.
`read_blocks()` preserves the previous API's choice between calling-thread execution and worker threads.
The reader keeps an offset table only after `build_index()` succeeds.
It keeps no saved index file or scan position between requests.

## Questions and options

The user selected the choices below.
The implementation also supports the requested comparison between scans and in-memory indexes.

| Question | Selected option | Other options and effects |
| --- | --- | --- |
| Which index selects a block? | Use the existing file block index. Saved `block.index` values remain valid for the same file contents. | Count only `OSMData` blocks. This requires a separate index name and conversion rules. |
| How does the reader locate a block? | Support scans from the file start and optional in-memory offset tables. | A saved index file requires source identity and format checks. |
| How does the application receive data? | Use `pbf_block_handler_t`. Its views remain valid during the callback. | Return an object that owns the data. This requires a new storage and lifetime contract. Both forms increase API scope. |
| Must reads run at the same time? | Permit separate readers to serve simultaneous requests. | Permit only one active reader, or support simultaneous requests on one shared reader. |
| Where does sequential block input belong? | Move `input_pbf_blocks()` into `pbf_reader_t` as `read_blocks()`. | Keep a separate function or a compatibility wrapper. Neither forms part of this proposal. |
| How does `input_pbf()` read entities? | Use the reader and preserve primitive-group entity batches for sequential speed. | Complete-block entity batches increased planet processing time. The user rejected that tradeoff. |

| Additional question | Implemented choice | Alternative and effect |
| --- | --- | --- |
| How does the API report failure? | Return `bool` and use the existing log callback. | Return a status enum that separates an invalid index, a non-data block, cancellation, and errors. |
| How does a callback identify parser context? | Use the block argument. Keep global parser state outside the new read path. | Change global context variables to thread-local variables. This changes the existing public interface and requires a separate compatibility review. |

The following API uses both additional choices.

## Implementation before this change

| Location | Previous behavior | Required change |
| --- | --- | --- |
| [inputosm.h](../include/inputosm/inputosm.h), `pbf_block_t` | Supplies the index, file offset, entity spans, string table, and conversion parameters. | Keep this block type and its field meanings. |
| `input_pbf_blocks()` | Opens a file and calls a handler for each data block. | Replace the free function with `pbf_reader_t::read_blocks()` on an open reader. |
| [inputosmpbf.cpp](../src/inputosmpbf.cpp), `mapping_t` | Maps a regular file for one sequential operation. | Keep a mapping for the reader lifetime. |
| `read_descriptor()` | Checks the length prefix and `BlobHeader`. Gets the Blob span without decoding its payload. | Use it for each request's scan. |
| `validate_header()` | Checks the initial header and rejects unsupported required features. | Run it during `open()`. |
| `blob_data()` and `decoder_t` | Decode raw or Zlib data into block storage. | Use them for each selected `OSMData` block. |
| `context_t::process()` | Decodes a block and selects entity or block callbacks. Also manages cancellation. | Keep complete block decoding and cancellation. Use one internal batch callback for entity delivery. |
| `read_file()` | Owns the mapping, scans the file, and supplies a bounded worker queue. | Separate mapping ownership from scanning and worker execution. |
| `input_pbf()` | Supplies global entity handlers to the internal executor. | Construct a reader and adapt its block callbacks to entity callbacks. |
| `run()` | Sets global parser context and starts the internal executor. | Move the required global setup into `input_pbf()`. Remove this separate entry path. |
| Global thread configuration functions | Supply one thread setting for PBF input. | Keep them for `input_file()`. Add equivalent settings to each reader. |

The previous sequential reader released its mapping when the input call returned.
It had no public reader object.
The new API adds that lifetime and an optional offset table.

## Block definition and numbering

A PBF file block has a length prefix, a `BlobHeader`, and a serialized `Blob`.
The header supplies the block type and Blob size.
The optional `indexdata` field has no defined offset-table structure in the schema.
This proposal does not depend on that field. See the [file schema](https://github.com/openstreetmap/OSM-binary/blob/master/osmpbf/fileformat.proto).

Each `PrimitiveBlock` contains its own string table and decoding parameters.
It permits decoding without earlier data blocks. See the [OSM data schema](https://github.com/openstreetmap/OSM-binary/blob/master/osmpbf/osmformat.proto).
This property permits selected block decoding after the reader locates the block and checks the initial file header.

The selected index counts every file block from zero.
It does not identify an OSM entity, a primitive group, a string table entry, or a byte offset.

| File contents in order | Index | Result from `read_block()` |
| --- | --- | --- |
| Initial `OSMHeader` | 0 | `false`; no block callback. |
| First `OSMData` | 1 | One callback after successful decoding. |
| Unknown block type | 2 | `false`; no block callback. |
| Second `OSMData` | 3 | One callback after successful decoding. |

Unknown block types occupy index positions.
An `OSMData` block with no entities also occupies an index position and causes one callback.
The reader never substitutes the next data block for a requested non-data block.

`file_offset` continues to identify the four-byte length field.
An index remains valid only for the file contents from which it came.
A new export can have different block boundaries even when its entities are the same.

## Public API

Add these declarations to `include/inputosm/inputosm.h`.
The new API replaces the free `input_pbf_blocks()` declaration and implementation.
The existing declarations for `pbf_block_t` and `pbf_block_handler_t` remain applicable.
The private implementation keeps mapping and decoder types out of the public header.

```cpp
#include <cstddef>
#include <memory>

namespace input_osm
{

class pbf_reader_t
{
public:
    pbf_reader_t() noexcept;
    ~pbf_reader_t();

    pbf_reader_t(const pbf_reader_t&) = delete;
    pbf_reader_t& operator=(const pbf_reader_t&) = delete;
    pbf_reader_t(pbf_reader_t&&) noexcept;
    pbf_reader_t& operator=(pbf_reader_t&&) noexcept;

    bool open(const char* filename) noexcept;
    void close() noexcept;

    bool is_open() const noexcept;

    void set_thread_count(size_t count) noexcept;
    void set_max_thread_count() noexcept;
    size_t thread_count() const noexcept;

    bool build_index() noexcept;
    bool has_index() const noexcept;
    size_t index_memory_bytes() const noexcept;

    bool read_blocks(
        bool decode_metadata,
        const pbf_block_handler_t& handler) noexcept;

    bool read_block(
        size_t index,
        bool decode_metadata,
        const pbf_block_handler_t& handler) noexcept;

private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
    size_t configured_threads_ = 1;
};

}
```

Each handler parameter is a const reference because the reader uses it only during the call.
Function pointers and copyable lambdas can use the existing `std::function` handler type.
The reader does not keep the handler after either read method returns.
`read_blocks()` joins all worker threads before returning.

### Sequential block input and migration

`read_blocks()` replaces the free function's filename parameter with the reader's existing file mapping.
It retains the metadata parameter, complete block callback, Boolean result, and callback lifetime rules.
The implementation removes the free function without a compatibility wrapper.
This change requires source changes and a rebuild for applications that use `input_pbf_blocks()`.

1. Construct a `pbf_reader_t` object.
2. If the application needs worker threads, set the reader's thread count.
3. Open the file with `reader.open(filename)`.
4. Replace the free function call with `reader.read_blocks(decode_metadata, handler)`.
5. Replace global parser-context reads in block handlers with the rules in this proposal.
6. Rebuild the application with the new headers and library.

```cpp
#include <inputosm/inputosm.h>

bool process_all_blocks(
    const char* filename,
    bool decode_metadata,
    size_t workers,
    const input_osm::pbf_block_handler_t& handler)
{
    input_osm::pbf_reader_t reader;
    reader.set_thread_count(workers);
    if (!reader.open(filename)) return false;
    return reader.read_blocks(decode_metadata, handler);
}
```

`input_file()` remains available for entity callbacks.
Its global thread configuration functions retain their current meanings.
The `input_pbf()` adapter copies that thread setting into its temporary reader.
Directly constructed reader objects use their own settings.
The previous [block API proposal](pbf-block-callback-proposal.md) records the implemented free function.
This proposal defines its replacement and the new reader's thread and parser-context rules.

### Top-level PBF entity adapter

The call path is `input_file()` to `input_pbf()` to `pbf_reader_t::read_blocks()` to the entity handlers.
The internal `input_pbf()` function constructs a temporary reader.
It copies the metadata setting, thread count, and entity handlers before reading starts.
It supplies a handler even when all entity handlers are empty, so the reader still validates the file.

Planet measurements showed a sequential regression when the adapter collected complete blocks.
The user selected primitive-group entity batches to preserve sequential speed.
A private `read_blocks()` overload selects these batches for `input_pbf()`.
Both public block methods always supply complete blocks.
All paths use the same descriptor parser, decoder, queue, and cancellation code.

The adapter uses this order for each primitive group:

| Order | Condition | Call |
| --- | --- | --- |
| 1 | The group contains ordinary or dense nodes, and a node handler exists. | `nodes(block.nodes)` |
| 2 | A way handler exists, and the preceding call succeeded. | `ways(block.ways)` |
| 3 | A relation handler exists, and the preceding calls succeeded. | `relations(block.relations)` |

The spans contain entities from that group only.
The adapter passes them without copying entities or string bytes.
Supplied way and relation handlers also receive empty spans.
An empty node group causes an empty node callback.
A block without primitive groups causes no entity callbacks.
These rules preserve the previous entity batch boundaries and order.

A handler result of `false` or an exception stops subsequent entity calls for that group.
The reader stops pending work and joins all worker threads before returning `false`.
Callbacks that already started can finish during cancellation.
If a later group is invalid, earlier groups can already have produced entity callbacks.
Public block callbacks receive no partial block after a decoding failure.

The adapter uses storage for one primitive group per active worker.
Public block callbacks require storage for a complete block per active worker.
All spans and their referenced data remain valid only during their callback.
The existing `input_file()` signature and XML delivery rules remain unchanged.

### Shared file mapping

Readers of the same file share one immutable mapping.
The registry compares device, inode, size, modification time, and status-change time.
It holds weak references, so it does not keep closed files mapped.
The last reader releases the mapping.
Opening a reader briefly locks the registry.
The reader checks file identity before creating a mapping.
Concurrent opens of the same unchanged file create only one mapping.
Read operations do not acquire that lock.
Each reader retains its own decoder, offset table, thread settings, and cancellation state.
Sequential workers construct their decoder state in their own thread.
Random reads use the requesting reader's retained decoder in the calling thread.

The registry reduces duplicate page faults and kernel page-table storage for simultaneous readers.
The source file must remain unchanged while any reader uses it.
Replacing a pathname with a different inode does not replace an existing reader's mapping.

### Reader lifetime

| Operation or state | Contract |
| --- | --- |
| Construction | The reader starts closed with a thread count of one. Construction does not allocate block storage. |
| `open()` on a closed reader | Map the file and check the initial header. Return `true` after both steps succeed. |
| Failed `open()` on a closed reader | Release temporary resources. Keep the reader closed. |
| `open()` on an open reader | Return `false`. Keep the existing file mapping. |
| `is_open()` | Return whether the reader has a mapping with a checked initial header. |
| `close()` | Release the decoder, offset table, and mapping reference. Keep the reader's thread setting. A second close has no effect. |
| Destruction | Release the resources as for `close()`. Report cleanup errors through the existing log callback. |
| Move construction or assignment | Transfer resources and thread settings. Leave the source closed with a thread count of one. First release destination resources during assignment. |

The reader does not keep the filename pointer.
It accesses the same mapping for the complete open lifetime.
It does not reopen the pathname for each request.
A file that contains only a valid header opens successfully.
It has no readable data blocks.
The API has no block-count query because a count requires a complete scan.
`read_blocks()` starts a new scan from the file start on every call.
Without an offset table, `read_block()` also starts a new scan.
Applications can alternate `read_blocks()` and `read_block()` calls on the same open reader.
Completion, cancellation, and read errors leave the mapping open.

### Optional offset table

`build_index()` scans all file block headers and stores their length-prefix offsets in `std::vector<uint64_t>`.
Vector positions retain the existing file block indexes, including the initial header and unknown block types.
The method checks framing through the exact file end without decoding data payloads.
A malformed later frame causes index construction to fail.
An invalid data payload does not prevent index construction when its framing is valid.

The reader installs the table only after the complete scan succeeds.
A failed construction leaves the reader open without a partial table.
A second call returns `true` without another scan when a complete table exists.
`has_index()` returns whether the table exists.
`index_memory_bytes()` returns allocated vector capacity in bytes, excluding allocator bookkeeping.
Both queries return zero or `false` for a closed reader.

With a table, `read_block()` finds the byte range by vector lookup and checks the selected descriptor within that range.
It performs the same payload decoding and callback as scan mode.
Unavailable indexes fail without another file scan.
Payload decoding failures do not invalidate the offset table.
Close releases the table, and move operations transfer its ownership.
Separate readers own separate tables.

`read_blocks()` continues to use a bounded scan queue, whether or not an offset table exists.
This avoids an index construction cost for sequential input.
The performance report compares index construction, subsequent reads, total time, and memory use.

### Sequential read result

`read_blocks()` returns `true` only when the complete scan and all callbacks succeed.
It returns `false` for a closed reader, an empty handler, cancellation, or an error.
A file with only a valid header produces `true` without a block callback.
During successful completion, each `OSMData` block causes one callback, including a data block with no entities.
The method counts unknown block types in file indexes but skips their payloads and callbacks.
It rejects a second `OSMHeader` and invalid framing anywhere that its scan reaches.
No callback receives a block with a decoding error.

With one thread, callbacks run in file order in the calling thread.
With multiple threads, callbacks can overlap and finish out of file order.
The method uses one handler object for the complete operation.
Its worker threads can call that object simultaneously.

A handler result of `false` or a handler exception stops this operation and produces `false`.
Callbacks with prior permission can still finish after a stop request.
With one thread, no subsequent callback runs after a stop request.
Every return path joins all workers and discards queued descriptors.
A new read operation starts with a new queue and cancellation state.

### Indexed read result and common data lifetime

`read_block()` returns `true` only after decoding succeeds and the handler returns `true`.
The callback runs once in the calling thread.
It receives the requested file block index and the corresponding file offset.

| Condition | Result | Callback count |
| --- | --- | --- |
| Valid data block; handler returns `true` | `true` | 1 |
| Valid data block; handler returns `false` | `false` | 1 |
| Handler throws an exception | `false`; log the exception | 1 |
| Closed reader, empty handler, or end of file before the requested index | `false`; log the cause | 0 |
| Header block or unknown block type | `false`; log the block type | 0 |
| Invalid framing before or at the requested index | `false`; log the cause | 0 |
| Invalid Blob, unsupported encoding, or invalid entity data | `false`; log the cause | 0 |
| Allocation failure before the callback | `false`; log the cause | 0 |

Failures that occur before the function call, such as handler construction failures, remain the caller's responsibility.
Log messages include the requested index and the failing block's index and offset when available.
The Boolean result does not distinguish cancellation from other failures.

For both methods, the metadata parameter applies to the current call only.
All entity values, string table rules, and conversion parameters follow the [existing block contract](pbf-block-callback-proposal.md).
The reader decodes all primitive groups before it calls the handler.

All spans, strings, and referenced data remain valid only during the callback.
A copy of `pbf_block_t` does not extend that lifetime.
To keep data after the callback, copy all necessary referenced data into application storage.

A failed read does not close the reader or prevent subsequent requests.
After a decoder or callback failure, the implementation discards retained decoder storage.
Worker decoder storage belongs to the sequential operation and ends when its workers finish.
This prevents incomplete decoder state or cancellation state from affecting another block.
The application retains responsibility for any changes that its callback made before a failure.
Failure or cancellation on one reader does not stop another reader.

### Example

The example accepts an index that the application obtained from an earlier block callback.
Both operations must use the same file contents.

```cpp
#include <inputosm/inputosm.h>

bool count_selected_block(
    const char* filename,
    size_t saved_index,
    size_t& node_count)
{
    input_osm::pbf_reader_t reader;
    if (!reader.open(filename)) return false;

    return reader.read_block(
        saved_index, false,
        [&](const input_osm::pbf_block_t& block) {
            node_count = block.nodes.size();
            return true;
        });
}
```

For repeated requests, keep the reader open between calls to either read method.
Requests can use any order and can repeat an index.
Each `read_block()` call decodes its selected block again.

## Scan and decoding

Each reader keeps a mapping and decoder storage in its private implementation.
Each scan operation creates its own cursor, file block index, and byte offset.
These scan values start at zero on every scan request.
The reader does not resume a previous scan, including after a successful read.

For each descriptor, the next byte offset is:

```text
next_offset = current_offset + 4 + blob_header_size + serialized_blob_size
```

Both scan methods count the initial header and unknown block types.
The indexed scan advances over earlier Blob payloads without calling `blob_data()` for those payloads.
The initial header contents need decoding only during `open()`.

### Open procedure

1. Reject a reader that is already open.
2. Open the file and check its type and size.
3. Acquire the shared file mapping.
4. Read the first descriptor at offset zero.
5. Require `OSMHeader` as the first block type.
6. Decode its Blob with `blob_data()`.
7. Check its contents with `validate_header()`.
8. Transfer the checked mapping reference to the reader.

Use subtraction for range checks before adding lengths to offsets.
Check that offsets fit the mapping size and pointer arithmetic limits.
The file size limit also bounds the number of block indexes.
Keep the existing `BlobHeader` size limit and nonnegative serialized Blob size checks.

### Indexed read procedure without an offset table

1. Check the reader state and handler.
2. Create a scan cursor at the start of the mapped file.
3. Set the current file block index and byte offset to zero.
4. If the cursor reaches the file end, return `false` for an unavailable index.
5. Read the next descriptor with the existing framing and range checks.
6. Require `OSMHeader` at index zero.
7. Reject an `OSMHeader` at any later index.
8. If the current index equals the requested index, continue at step 12.
9. Calculate the next byte offset from the checked file block size.
10. Increase the current file block index by one.
11. Repeat from step 4 with the cursor after the previous Blob.
12. Require `OSMData` as the selected block type.
13. Decode the selected Blob with the existing raw or Zlib decoder.
14. Prepare the decoder with this request's metadata setting, index, and offset.
15. Decode all primitive groups into complete block storage.
16. Complete the entity spans and string table views.
17. Call the handler once.
18. Return the handler result without scanning later blocks.

### Sequential read procedure

1. Check the reader state and handler.
2. Copy the reader's thread setting into the operation context.
3. Create a new scan cursor at the start of the mapping.
4. Create operation-local queue and cancellation state.
5. Check the initial descriptor without decoding the checked header payload again.
6. If the thread count exceeds one, start the configured worker threads.
7. Scan subsequent descriptors with the existing framing, size, and file index checks.
8. Reject a second `OSMHeader`.
9. For an unknown block type, skip its payload and callback.
10. With one thread, decode each data block in the calling thread.
11. With multiple threads, add each data block descriptor to the bounded queue.
12. Call the handler once for each complete decoded block.
13. On cancellation or failure, stop the scan and pending work.
14. Join all worker threads before the operation returns.
15. Return whether the scan and all callbacks succeeded.

The descriptor queue holds at most twice the operation's worker count.
Each worker has separate decoder storage and uses the reader's unchanged mapping.
The calling-thread path can reuse the reader's retained decoder storage.
Partial worker startup failures also require cancellation and joins for all started workers.
The method keeps no descriptors or scan position after returning.

The shared helper covers complete block decoding and span construction without global parser-state access.
Callback invocation and error handling remain the responsibility of each input operation.
An operation-local cancellation check preserves the sequential reader's stop checks between primitive groups.
The indexed read operation supplies a check that permits decoding to continue until its single callback.
The shared scan executor accepts an existing mapping, explicit thread count, and operation context.
`read_blocks()` supplies the reader's mapping and settings.
`input_pbf()` calls that method through its temporary reader and supplies only a group-to-entity adapter.
Remove the executor's entity-handler fields and direct entity delivery branch.
All PBF entry paths use the same decoder. Sequential paths also use the same worker execution.
Only `build_index()` allocates the offset table.
The decoder records tag ranges only for nodes with tags.
The bounded queue uses fixed vector storage and separate condition variables for data and capacity.
Common `OSMData` headers use a checked fast path.
Other field layouts use the general Protocol Buffers parser.

## Validation limits and file changes

`open()` checks the initial descriptor and decodes the initial `OSMHeader`.
It does not decode any `OSMData` payload.
Without a table, each indexed request checks framing from the file start through the selected block.
The following table describes `read_block()` without an offset table.

| Defect location | Effect |
| --- | --- |
| Initial file header | `open()` fails. |
| Framing before or at the requested index | The request fails. The scan cannot establish the requested block boundary. |
| Payload of an earlier data block | The request can succeed. It does not decode that payload. |
| Selected data payload | The request fails before the callback. |
| Framing or payload after the selected block | The request can succeed. It does not examine later blocks. |
| Second `OSMHeader` before or at the selected index | The request fails. |

An unsupported data compression method affects only a request that selects that data block.
A successful request does not prove that the complete file is valid.
Without a table, an out-of-range request scans to the file end unless an earlier framing error stops it.
Unknown block payloads remain opaque during scans.

`read_blocks()` checks framing and decodes each data block that the scan reaches before cancellation or failure.
Trailing malformed framing makes `read_blocks()` fail even when earlier callbacks succeed.
A complete successful sequential read checks all data blocks with the selected metadata setting.

Selected blocks use the current raw size limits, decompression checks, string checks, and integer range checks.
The new API adds no compression formats or required feature support.

Keep the source file contents and size unchanged while the reader is open.
All readers of the file refer to those contents for their complete open lifetimes.
A read-only mapping does not prevent another process from changing or truncating the source file.
The first version does not supply a file snapshot or concurrent file-change detection.

## Threads and parser state

Separate reader objects can open and read the same file or different files simultaneously.
This includes any combination of `read_blocks()` and `read_block()` calls on separate readers.
Each reader has its own scan cursor, decoder, decompression buffer, and entity storage.
Readers of the same file share the immutable mapping.
Each reader keeps all mutable parser storage in its own implementation.
Cancellation and failures affect only the request that produces them.

Each indexed read runs in the calling thread and creates no worker threads.
Sequential reads use the thread setting that belongs to their reader.
Neither method reads or changes global thread configuration.
Applications provide separate calling threads for simultaneous reader operations.

| Member | Thread configuration contract |
| --- | --- |
| `set_thread_count(count)` | Set this reader's sequential thread count. Treat zero as one. Limit the result to the hardware thread count. |
| `set_max_thread_count()` | Select the hardware thread count for this reader. |
| `thread_count()` | Return this reader's effective count. The minimum is one. |

If the hardware thread count is zero, use one as the maximum.
The reader permits configuration while closed or between read operations.
Open and close operations preserve its thread setting.
`read_blocks()` copies this setting once at entry.
`read_block()` uses one calling thread regardless of that setting.

| State | New reader contract |
| --- | --- |
| `decode_metadata` and entity handler globals | No access. The request supplies metadata and its handler directly. |
| `file_type` and `osc_mode` | No access or modification. These globals do not describe the new reader's callback. |
| `block_index` | Set this thread-local value to the callback's `block.index`. Restore its previous value after the callback. |
| `thread_index` | Use zero for indexed reads and sequential reads with one thread. For sequential workers, use their operation-local worker index. |
| Thread configuration | Use reader-local settings only. Global settings continue to apply to `input_file()`. |
| Log configuration | Shared, read-only access during operations. Configuration changes require all operations to finish. |

The callback's reader type identifies PBF input and bulk mode.
Use `block.index` for block identity.
Do not use `file_type` or `osc_mode` as context for the new API.
Do not use `thread_index` as a unique identifier across separate readers.
Sequential worker indexes range from zero through the operation's thread count minus one.
The implementation restores `thread_index` after each callback as it does for `block_index`.
For per-reader counters, capture separate counter storage in each handler.
Within a sequential operation, use synchronization or a separate counter for each worker.

The implementation restores thread-local values even when a handler throws.
For `input_file()` consumers, `input_pbf()` sets `file_type` and `osc_mode` before it opens the temporary reader.
All entity calls for one group receive the same `thread_index` and `block_index` context.
Only the adapter performs this compatibility setup.
The log callback must support simultaneous calls and must not throw.
If application handlers share mutable data, protect that data with synchronization.

Permit only one public read operation on an individual reader at a time.
Do not call other reader members while either read method is active.
Do not close, move, or destroy a reader during its callback.
Do not start an input operation from an input callback.
Do not run an independent reader operation while `input_file()` is active.
The temporary reader inside `input_pbf()` forms part of that same top-level operation.
`input_file()` retains its current restrictions on simultaneous input operations.

This scope supports simultaneous reader objects without changing the existing global variable declarations.
Simultaneous operations on one reader, or independent operations alongside `input_file()`, require a separate concurrency contract.

## Cost and alternatives

Let `B` be the number of all file blocks.
Let `k` be the requested file block index.
Let `H(k)` be the number of prefix and `BlobHeader` bytes through index `k`.
Let `S` be the complete decoding cost of one selected block.

| Design | Initial work | Work for a request | Main cost |
| --- | --- | --- | --- |
| Scan for each request | Map the file and check the initial header | `O(k + 1 + H(k)) + S` | Repeats descriptor reads. No retained table. |
| Optional complete offset table | Scan all descriptors once | `O(1)` lookup plus selected header parsing and `S` | At least eight bytes per file block for an offset table. |
| Table built up to the largest requested index | Scan as requests require | Constant lookup for known entries; further scanning for new entries | Variable request latency and partial file validation. |
| Separate saved index file | Build once, then load and verify | Lookup plus selected header parsing and `S` | Index format, source identity, and corruption checks. |

Scan mode uses constant scan-state memory in addition to the mapping and decoder storage.
Scan mode does not allocate storage in proportion to `B`.
Index mode adds eight bytes per block, plus vector capacity overhead.
Access by index is available, but lookup time grows with the requested index.
Reading all blocks through separate requests repeats earlier scans and can require quadratic descriptor work.
Use `reader.read_blocks()` when the application needs all blocks.
That method scans the descriptors once per call and decodes each data block once during successful completion.

The request scan advances over earlier Blob payloads without decoding or copying them.
Storage read-ahead and memory page faults can still cause additional I/O.
The design does not promise constant-time disk access or an I/O-free scan.

Each complete file mapping consumes virtual address space.
Resident memory depends on accessed pages and operating system behavior.
Readers of the same file share an immutable mapping but have separate decoder allocations.
Each reader's decoder memory can remain at its largest successful request size until `close()`.
Sequential worker storage ends when the operation returns.
PBF `input_file()` retains storage for one primitive group per active worker.
Public block callbacks retain complete block storage.
The adapter adds no entity copies after block decoding.
Several simultaneous sequential readers can create the sum of their configured worker counts.
Select per-reader thread counts that fit the application's memory and processor limits.
The first version has no decoded-block cache.

The current memory mapping approach minimizes implementation changes.
An alternative based on `pread()` can avoid a complete mapping but requires file-range I/O and owned raw block buffers.
Remote files and non-seekable input need separate I/O contracts.

A saved index requires a versioned format, fixed integer encoding, entry checks, and a source identity policy.
File size and modification time alone cannot prove that source bytes match a saved index.
A full content hash gives a stronger check but requires a complete file read.
The saved-index option must define that tradeoff before implementation.

## Implementation and verification plan

1. Add the public reader declarations and private resource owner.
2. Add initial header validation for `open()`.
3. Extract complete block decoding without global parser-state access.
4. Add a new scan from the file start for each selected-block request.
5. Add per-reader thread configuration.
6. Move sequential block execution into `pbf_reader_t::read_blocks()` with operation-local worker state.
7. Implement `input_pbf()` with a temporary reader and a group-to-entity adapter.
8. Remove the separate entity delivery branch and its executor handler fields.
9. Remove the free `input_pbf_blocks()` API.
10. Migrate its tests and integration examples to the reader methods.
11. Test primitive-group entity batches, callback order, and cancellation.
12. Add tests for numbering, lifetime, failures, repeated requests, and simultaneous readers.
13. Run the PBF, XML, and OSC tests after the decoder changes.
14. Add an integration example that reads one selected block.
15. Add the implemented API contract and migration instructions to the README.
16. Measure open time, sequential input, descriptor scanning, and selected block decoding separately.

| Planned file | Change |
| --- | --- |
| `include/inputosm/inputosm.h` | Add `pbf_reader_t`, both read methods, and member thread configuration. Remove the free block input declaration. |
| `src/inputosmpbf.cpp` | Move block input into the reader. Implement `input_pbf()` through `read_blocks()`. Remove the separate entity input path. |
| `src/inputosm.cpp` | Keep the `input_file()` dispatch to `input_pbf()` and the existing handler configuration. |
| `test/unit/pbf_block_test.cpp` | Migrate sequential block tests to `read_blocks()`. Update entity tests for the adapter contract. |
| `test/unit/pbf_reader_test.cpp` | Add focused random access tests. |
| `test/unit/pbf_test_data.h` | Reuse existing raw, Zlib, and unknown-type fixture helpers. Extend them only where necessary. |
| `test/unit/CMakeLists.txt` | Register the new test. |
| `test/integration/read_block.cpp` | Add an example that reads one block by index. |
| `test/integration/count_blocks.cpp` | Replace the free function with reader construction, open, and `read_blocks()`. |
| `test/integration/CMakeLists.txt` | Register the example. |
| `README.md` | Describe both reader methods and migration. Document PBF entity batch boundaries, empty spans, callback order, and memory use. |

The required acceptance checks are:

| Case | Expected result |
| --- | --- |
| Sequential and random reads of the same block | Equal indexes, offsets, entities, strings, and block parameters. Compare values rather than pointer addresses. |
| Requests in reverse order, with duplicates | Each callback receives only its requested block. No earlier data block requires decoding. |
| Unknown block between data blocks | Later blocks keep the same indexes as the sequential API. |
| Index zero, unknown type, and index beyond the last block | `false` without a block callback. |
| Valid header with no data blocks | Open succeeds. `read_blocks()` succeeds without callbacks. Indexed data requests fail. |
| Raw and Zlib blocks; ordinary and dense nodes | Results match the existing decoder behavior. |
| Empty entities and several primitive groups | One callback contains the complete selected block. |
| Metadata enabled and disabled on successive requests | Each result follows its own metadata setting. |
| Malformed framing before the requested block | The request fails before any block callback. |
| Malformed framing after a valid requested block | Open and the selected request succeed. A request that reaches the defect fails. |
| Malformed unselected data payload with valid framing | Open and other valid block requests succeed. Selecting the malformed block fails. |
| Unsupported initial required feature | Open fails before any data decoding. |
| Handler returns `false` or throws | The request fails. A later valid request can succeed. |
| Decoder failure followed by another valid request | No incomplete data or cancellation state reaches the next callback. |
| Separate readers with simultaneous callbacks | Both callbacks complete with their own block data and metadata settings. Neither waits for a library-wide read lock. |
| Simultaneous sequential and indexed reads on separate readers | Each method follows its own callback and thread rules. Both use independent storage and cancellation state. |
| Two sequential readers with different thread settings | Each uses its own configured worker count and bounded queue. Global thread settings have no effect. |
| One reader fails while another callback is active | The other reader's views and result remain valid. |
| Different global parser-state values before a request | The request does not change those globals. It restores thread-local values after success or failure. |
| Repeated or increasing requested indexes without a table | Each scan starts at offset zero. No previous cursor reduces the scan. |
| Sequential read, indexed read, then another sequential read on one reader | Each operation starts at the file start. Both sequential reads deliver all blocks after successful completion. |
| Sequential cancellation or worker startup failure | All started workers finish before return. A later read starts with new queue and cancellation state. |
| Member thread configuration across close, reopen, and move | Thread settings follow the configuration and lifetime contracts. |
| Close, repeated close, failed open, reopen, and move | State and resource ownership follow the lifetime table. |
| Large offsets and size arithmetic near limits | Valid offsets above 4 GiB work on supported systems. Overflow and out-of-range values fail safely. |
| Migrated sequential block API | `read_blocks()` retains block contents, callback order rules, bounded queues, and cancellation behavior. |
| PBF entity adapter with several primitive groups | Entity handlers retain primitive-group batches. Combined entity values match `read_blocks()`. |
| Missing entity handlers or empty entity spans | Skip absent handlers. Preserve the previous primitive-group rules for empty spans. |
| All entity handlers absent | The adapter still scans and validates all blocks. Valid input succeeds. |
| Node or way handler returns `false` or throws | No later entity handler runs for that block. `input_pbf()` returns `false` after all workers finish. |
| Invalid later group in a data block | Earlier entity batches can already have reached callbacks. Public block callbacks receive no partial block. |
| Adapter metadata, thread count, and parser context | Copied settings reach the temporary reader. Entity callbacks receive the block's index and worker context. |
| PBF entity adapter implementation | The top-level function uses `pbf_reader_t::read_blocks()`. No independent entity scan or decode path remains. |
| XML and OSC input | Existing signatures, entity values, and callback delivery remain valid. |

Use the existing temporary fixture helpers for deterministic tests.
Use malformed preceding payloads to verify that indexed requests decode only the selected data block.
Compare scan and table modes against the same sequential block snapshots.
Use bounded synchronization in concurrency tests to verify overlapping callbacks without indefinite waits.
Run the concurrency tests with ThreadSanitizer where the build environment supports it.
Test callback view contents while their lifetime is valid.
Do not access expired views to test lifetime limits.
Measure open time and early, middle, and late block requests with a representative large file.
Measure one reader and several simultaneous readers separately.
Measure PBF `input_file()` peak memory and processing time with primitive-group batches.
Report cold-cache and warm-cache measurements separately.
