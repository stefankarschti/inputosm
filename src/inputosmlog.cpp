#include "inputosmlog.h"

#include <cstdarg>
#include <cstdio>

static input_osm::log_callback_t g_default_log_callback = [](input_osm::log_level_t, const char*){};

namespace input_osm {

log_level_t g_log_level = LOG_LEVEL_TRACE;
log_callback_t g_log_callback = g_default_log_callback;

void set_log_level(log_level_t level) noexcept
{
    g_log_level = level;
}

bool set_log_callback(log_callback_t log_callback) noexcept
{
    if(!log_callback)
    {
        return false;
    }
    g_log_callback = log_callback;
    return true;
}

void log(log_level_t level, const char* fmt, ...) noexcept
{
    if(level < g_log_level)
    {
        return;
    }

    static constexpr int k_buffer_size = 1 << 9;
    char buffer[k_buffer_size];
    va_list args;
    va_start(args, fmt);

    vsnprintf(buffer, k_buffer_size, fmt, args);

    va_end(args);

    g_log_callback(level, buffer);
}

} // namespace input_osm
