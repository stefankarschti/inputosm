#include <inputosm/inputosm.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
int run()
{
    input_osm::set_thread_count(1);
    if (input_osm::thread_count() != 1)
    {
        std::cerr << "thread_count should be exactly 1 after set_thread_count(1)\n";
        return EXIT_FAILURE;
    }

    input_osm::set_thread_count(std::numeric_limits<size_t>::max());
    const auto hardware_limit = static_cast<size_t>(std::thread::hardware_concurrency());
    const auto expected_limit = hardware_limit ? hardware_limit : static_cast<size_t>(1);
    if (input_osm::thread_count() != expected_limit)
    {
        std::cerr << "thread_count should clamp to hardware_concurrency when set_thread_count receives a large value\n";
        return EXIT_FAILURE;
    }

    input_osm::set_thread_count(2);
    const auto expected_two = hardware_limit ? std::min<size_t>(2, hardware_limit) : static_cast<size_t>(1);
    if (input_osm::thread_count() != expected_two)
    {
        std::cerr << "thread_count should respect the hardware upper bound when limited to 2\n";
        return EXIT_FAILURE;
    }

    input_osm::set_max_thread_count();
    if (input_osm::thread_count() != expected_limit)
    {
        std::cerr << "set_max_thread_count should align with hardware_concurrency\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
} // namespace

int main()
{
    return run();
}
