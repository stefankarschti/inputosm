#ifndef _INPUTOSMTESTCOUNTER_H_
#define _INPUTOSMTESTCOUNTER_H_

#include <cstdint>

namespace input_osm
{

// since each thread works on one uint64, let's make sure these are not false-shared
template <typename T>
// T should be an integer type (like uint64_t, int32_t, etc...)
struct Counter
{
    static_assert(sizeof(T) < 64);

    Counter() = default;

    Counter(T c)
        : count(c)
    {
    }

    // to simplify using the algorithms with this type
    operator const T&() const { return count; }

    operator T&() { return count; }

private:
    // one cacheline worth, actual counter
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