#include "inputosmlog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

constexpr const char* kError = "err";
constexpr const char* kInfo = "inf";
constexpr const char* kTrace = "trc";

// basic printf logger callback, can be changed by the user
// NOTE: the function *should* be thread safe, interlaced output is ok-ish, but make sure
//      there's no other shared data
static input_osm::log_callback_t g_default_log_callback = [](input_osm::log_level_t lvl, const char* message) {
    switch (lvl)
    {
        case input_osm::log_level_t::LOG_LEVEL_TRACE:
            printf("[%s]: %s\n", kTrace, message);
            return;
        case input_osm::log_level_t::LOG_LEVEL_INFO:
            printf("[%s]: %s\n", kInfo, message);
            return;
        case input_osm::log_level_t::LOG_LEVEL_ERROR:
            printf("[%s]: %s\n", kError, message);
            return;
        default:
            return;
    }
};

namespace input_osm
{

log_level_t g_log_level = []() {
    if (const char* env_level = std::getenv("INPUTOSM_LOG_LEVEL"); env_level)
    {
        if (strcmp(env_level, kError) == 0)
        {
            return LOG_LEVEL_ERROR;
        }
        else if (strcmp(env_level, kTrace) == 0)
        {
            return LOG_LEVEL_TRACE;
        }
    }
    return LOG_LEVEL_INFO;
}();

log_callback_t g_log_callback = g_default_log_callback;

void set_log_level(log_level_t level) noexcept
{
    g_log_level = level;
}

bool set_log_callback(log_callback_t log_callback) noexcept
{
    if (!log_callback)
    {
        return false;
    }
    g_log_callback = log_callback;
    return true;
}

void log(log_level_t level, const char* fmt, ...) noexcept
{
    if (level < g_log_level)
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
