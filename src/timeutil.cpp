// Copyright 2021-2026 Stefan Karschti
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "timeutil.hpp"
#include <chrono>
#include <ctime>
#include <fmt/chrono.h>

int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t now_us()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

time_t str_to_timestamp(const char* str)
{
    struct tm timeinfo{};
    if (strptime(str, "%FT%TZ", &timeinfo) == nullptr)
    {
        return 0;
    }
    return timegm(&timeinfo);
}

time_t str_to_timestamp_osmstate(const char* str)
{
    struct tm timeinfo{};
    if (strptime(str, "%FT%H\\:%M\\:%SZ", &timeinfo) == nullptr)
    {
        return 0;
    }
    return timegm(&timeinfo);
}

std::string timestamp_to_str(const time_t rawtime)
{
    return fmt::format("{:%F %T}", fmt::gmtime(rawtime));
}

std::string duration_to_str(int64_t nano)
{
    if (nano < 1000l)
    {
        return fmt::format("{} ns", nano);
    }
    else if (nano < 1000000l)
    {
        return fmt::format("{:.3f} μs", nano / 1000.0);
    }
    else if (nano < 1000000000l) // Less than 1 second.
    {
        return fmt::format("{:.3f} ms", nano / 1000000.0);
    }
    else if (nano < 60000000000l) // Less than 60 seconds.
    {
        return fmt::format("{:.3f} s", nano / 1000000000.0);
    }
    else
    {
        int64_t seconds = nano / 1000000000;
        int64_t minutes = seconds / 60;
        seconds = seconds % 60;
        int64_t hours = minutes / 60;
        minutes = minutes % 60;

        if (hours > 0)
            return fmt::format("{} hours {} minutes {} seconds", hours, minutes, seconds);
        else
            return fmt::format("{} minutes {} seconds", minutes, seconds);
    }
}
