#include <inputosm/inputosm.hpp>

#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <fmt/chrono.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fmt::print("Usage {} <path-to-pbf>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const auto logWithTime = [](input_osm::log_level_t level, const char *message) {
        auto lvl_to_str = [](input_osm::log_level_t lvl) {
            switch (lvl)
            {
                case input_osm::LOG_LEVEL_TRACE:
                    return "TRC";
                case input_osm::LOG_LEVEL_INFO:
                    return "INF";
                case input_osm::LOG_LEVEL_ERROR:
                    return "ERR";
                default:
                    return "NON";
            }
        };

        struct timespec ts;
        timespec_get(&ts, TIME_UTC);
        fmt::print("{:%m/%d/%y %T}.{:06} UTC [{}]: {}\n",
                   fmt::gmtime(ts.tv_sec),
                   ts.tv_nsec / 1000,
                   lvl_to_str(level),
                   message);
    };

    input_osm::set_log_level(input_osm::LOG_LEVEL_TRACE);
    input_osm::set_log_callback(logWithTime);

    if (!input_osm::input_file(
            argv[1],
            true,
            [](std::span<const input_osm::node_t>) { return true; },
            [](std::span<const input_osm::way_t>) { return true; },
            [](std::span<const input_osm::relation_t>) { return true; }))
    {
        return EXIT_FAILURE;
    }
}
