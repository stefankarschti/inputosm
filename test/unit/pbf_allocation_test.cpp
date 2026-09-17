#include <inputosm/inputosm.h>
#include "pbf_test_data.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define INPUTOSM_TEST_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__) || defined(INPUTOSM_TEST_TSAN)
// ThreadSanitizer supplies its own allocation operators.
int main()
{
    return 77;
}
#else

namespace
{
std::atomic<bool> measure{false};
std::atomic<size_t> allocations{0};
} // namespace
void* operator new(size_t size)
{
    if (measure.load(std::memory_order_relaxed)) allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}
void* operator new[](size_t size)
{
    return ::operator new(size);
}
void operator delete[](void* pointer) noexcept
{
    ::operator delete(pointer);
}
void operator delete(void* pointer, size_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, size_t) noexcept
{
    std::free(pointer);
}

int main()
{
    using namespace input_osm;
    using namespace pbf_test;
    try
    {
        set_log_level(LOG_LEVEL_DISABLED);
        files_t files;
        const auto dense = packed(1, {2, 2}) + packed(8, {2, 2}) + packed(9, {2, 2}) + packed(10, {1, 2, 0, 0});
        const auto data = table() + message(2, message(2, dense)) +
                          message(2, message(3, integer(1, 3) + packed(8, {2, 2}))) +
                          message(2, message(4, integer(1, 4) + packed(8, {1}) + packed(9, {2}) + packed(10, {0})));
        const auto path = files.write(header() + zlib_block(data));
        pbf_reader_t reader;
        if (!reader.open(path.c_str()) || !reader.build_index()) return 1;
        const pbf_entity_options_t options{pbf_node_options_t{true, true, true, true, true},
                                           pbf_way_options_t{true, true, true, true},
                                           pbf_relation_options_t{true, true, true, true, true}};
        const pbf_block_handler_t handler = [&](const pbf_block_t& block) {
            pbf_counts_t counts;
            size_t strings;
            return block.counts(counts) && counts.nodes == 2 && counts.ways == 1 && counts.relations == 1 &&
                   block.string_table_size(strings) && strings == 3 &&
                   block.decode_strings([](auto) { return true; }) &&
                   block.decode_entities(options, [](const auto&) { return true; });
        };
        // Prepare each retained buffer before allocation measurement.
        if (!reader.read_block(1, handler)) return 1;
        if (!reader.decode_header([](const auto&) { return true; })) return 1;
        bool ok = true;
        measure.store(true);
        for (size_t i = 0; i < 100; ++i)
            ok = reader.read_block(1, handler) && reader.decode_header([](const auto&) { return true; }) && ok;
        measure.store(false);
        if (!ok || allocations.load() != 0)
        {
            std::fprintf(stderr, "Steady-state allocations: %zu\n", allocations.load());
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        measure.store(false);
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

#endif
