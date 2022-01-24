#ifndef _TIME_UTIL_H_
#define _TIME_UTIL_H_

#include <ctime>
#include <string>
#include <cstdint>

int64_t now_ms();
int64_t now_us();
int64_t time_ns();
time_t str_to_timestamp(const char* str);
time_t str_to_timestamp_osmstate(const char* str);
std::string timestamp_to_str(const time_t rawtime);
std::string duration_to_str(int64_t nano);

#endif
