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

#ifndef _TIMEUTIL_H_
#define _TIMEUTIL_H_

#include <cstdint>
#include <ctime>
#include <string>

int64_t now_ms();
int64_t now_us();
time_t str_to_timestamp(const char* str);
time_t str_to_timestamp_osmstate(const char* str);
std::string timestamp_to_str(const time_t rawtime);
std::string duration_to_str(int64_t nano);

#endif // _TIMEUTIL_H_
