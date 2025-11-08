#pragma once

#include <chrono>
#include <cstdint>

inline int32_t make_timestamp(int year_value,
                              uint8_t month_value,
                              uint8_t day_value,
                              uint8_t hour_value,
                              uint8_t minute_value,
                              uint8_t second_value)
{
    using namespace std::chrono;
    const sys_days date = year{year_value} / month{month_value} / day{day_value};
    const auto point = date + hours{hour_value} + minutes{minute_value} + seconds{second_value};
    return static_cast<int32_t>(system_clock::to_time_t(point));
}
