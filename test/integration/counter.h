#ifndef _INPUTOSMTESTCOUNTER_H_
#define _INPUTOSMTESTCOUNTER_H_

#include <cstdint>

namespace input_osm
{

// Each thread uses one counter. Use a different cache line for each counter to prevent false sharing.
template <typename T>
// T must be an integer type, such as uint64_t or int32_t.
struct Counter
{
    static_assert(sizeof(T) < 64);

    Counter() = default;

    Counter(T c)
        : count(c)
    {
    }

    // These conversions let algorithms use the counter value.
    operator const T&() const { return count; }

    operator T&() { return count; }

private:
    // Align the counter value to a 64-byte cache line.
    alignas(64) T count = 0;
};

using u64_64B = Counter<uint64_t>;
using i64_64B = Counter<int64_t>;
using u32_64B = Counter<uint32_t>;
using i32_64B = Counter<int32_t>;

static_assert(sizeof(u64_64B) == 64);
static_assert(sizeof(i64_64B) == 64);
static_assert(sizeof(u32_64B) == 64);
static_assert(sizeof(i32_64B) == 64);

} // namespace input_osm

#endif // _INPUTOSMTESTCOUNTER_H_