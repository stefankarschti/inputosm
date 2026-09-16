#include "inputosmlog.h"

#include <cstring>
#include <cstdlib>

constexpr const char* kError = "err";
constexpr const char* kInfo = "inf";
constexpr const char* kTrace = "trc";

// This default log callback uses fmt. The user can replace it.
// The callback must be thread-safe. Output from different threads can be mixed.
// Do not add other shared data without synchronization.
static input_osm::log_callback_t g_default_log_callback = [](input_osm::log_level_t lvl, const char* message) {
    switch (lvl)
    {
        case input_osm::log_level_t::LOG_LEVEL_TRACE:
            fmt::print("[{}]: {}\n", kTrace, message);
            return;
        case input_osm::log_level_t::LOG_LEVEL_INFO:
            fmt::print("[{}]: {}\n", kInfo, message);
            return;
        case input_osm::log_level_t::LOG_LEVEL_ERROR:
            fmt::print("[{}]: {}\n", kError, message);
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

} // namespace input_osm
