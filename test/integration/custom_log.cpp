#include <inputosm/inputosm.h>

#include <cstdlib>
#include <cstdio>
#include <ctime>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage %s <path-to-pbf>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const auto logWithTime = [](input_osm::log_level_t level, const char *message)
    {
        auto lvl_to_str = [](input_osm::log_level_t lvl)
        {
            switch(lvl)
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
        char time_buf[100];
        size_t rc = strftime(time_buf, sizeof(time_buf), "%D %T", gmtime(&ts.tv_sec));
        snprintf(time_buf + rc, sizeof(time_buf) - rc, ".%06ld UTC", ts.tv_nsec / 1000);

        printf("%s [%s]: %s\n", time_buf, lvl_to_str(level), message);
    };

    input_osm::set_log_level(input_osm::LOG_LEVEL_TRACE);
    input_osm::set_log_callback(logWithTime);

    using input_osm::span_t;
    if (!input_osm::input_file(
            argv[1], true, [](span_t<input_osm::node_t> node_list)
            { return true; },
            [](span_t<input_osm::way_t>)
            { return true; },
            [](span_t<input_osm::relation_t>)
            { return true; }))
    {
        return EXIT_FAILURE;
    }
}
