// Copyright 2021-2022 Stefan Karschti
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

#include "timeutil.h"
#include <chrono>
#include <ctime>
#include <cinttypes>

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
    struct tm* dt;
    char buffer[30];
    dt = gmtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%F %T", dt);
    return std::string(buffer);
}

std::string duration_to_str(int64_t nano)
{
    char buffer[256];
    if (nano < 1000l)
    {
        snprintf(buffer, 256, "%" PRId64 " ns", nano);
    }
    else if (nano < 1000000l)
    {
        snprintf(buffer, 256, "%.3f μs", nano / 1000.0);
    }
    else if (nano < 1000000000l) // < 1s
    {
        snprintf(buffer, 256, "%.3f ms", nano / 1000000.0);
    }
    else if (nano < 60000000000l) // < 60s
    {
        snprintf(buffer, 256, "%.3f s", nano / 1000000000.0);
    }
    else
    {
        int64_t seconds = nano / 1000000000;
        int64_t minutes = seconds / 60;
        seconds = seconds % 60;
        int64_t hours = minutes / 60;
        minutes = minutes % 60;

        if (hours > 0)
            snprintf(buffer, 256, "%" PRId64 " hours %" PRId64 " minutes %" PRId64 " seconds", hours, minutes, seconds);
        else
            snprintf(buffer, 256, "%" PRId64 " minutes %" PRId64 " seconds", minutes, seconds);
    }
    return std::string(buffer);
}