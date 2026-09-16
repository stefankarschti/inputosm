#include "inputosmlog.h"
#include "timeutil.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
std::string message;
input_osm::log_level_t message_level;
unsigned message_count = 0;

void receive_log(input_osm::log_level_t level, const char* text)
{
    message = text;
    message_level = level;
    ++message_count;
}

void check(bool condition, const char* description)
{
    if (condition) return;
    fmt::print(stderr, "Check failed: {}\n", description);
    std::exit(EXIT_FAILURE);
}
} // namespace

int main()
{
    using namespace input_osm;
    check(set_log_callback(receive_log), "Set the log callback");
    set_log_level(LOG_LEVEL_INFO);
    log(LOG_LEVEL_TRACE, "Filtered message {}", 1);
    check(message_count == 0, "Log level filter");

    log(LOG_LEVEL_ERROR, "PBF block {} at offset {}: {}", 7, 12345678900ULL, "Invalid data");
    check(message == "PBF block 7 at offset 12345678900: Invalid data", "Log message values");
    check(message_level == LOG_LEVEL_ERROR && message_count == 1, "Log callback level and count");

    const std::string long_message(700, 'x');
    log(LOG_LEVEL_INFO, "{}", long_message);
    check(message == long_message.substr(0, 511), "Log buffer limit and null terminator");

    check(timestamp_to_str(0) == "1970-01-01 00:00:00", "UTC timestamp");
    check(duration_to_str(999) == "999 ns", "Nanosecond duration");
    check(duration_to_str(1000) == "1.000 μs", "Microsecond boundary");
    check(duration_to_str(1234567) == "1.235 ms", "Millisecond precision");
    check(duration_to_str(1234567890) == "1.235 s", "Second precision");
    check(duration_to_str(60000000000) == "1 minutes 0 seconds", "Minute boundary");
    check(duration_to_str(3661000000000) == "1 hours 1 minutes 1 seconds", "Hour duration");
}
