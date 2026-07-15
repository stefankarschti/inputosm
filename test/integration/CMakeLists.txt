project(inputosm_integration_tests)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(count_all count_all.cpp)
target_link_libraries(count_all PRIVATE Threads::Threads inputosm::inputosm)

add_executable(statistics statistics.cpp)
target_link_libraries(statistics PRIVATE Threads::Threads inputosm::inputosm)

add_executable(extract_ferries extract_ferries.cpp)
target_link_libraries(extract_ferries PRIVATE Threads::Threads inputosm::inputosm)

add_executable(export_csv export_csv.cpp)
target_link_libraries(export_csv PRIVATE Threads::Threads inputosm::inputosm)

add_executable(lat_stat lat_stat.cpp)
target_link_libraries(lat_stat PRIVATE Threads::Threads inputosm::inputosm)

add_executable(export_db export_db.cpp)
target_link_libraries(export_db PRIVATE Threads::Threads inputosm::inputosm)

add_executable(custom_log custom_log.cpp)
target_link_libraries(custom_log PRIVATE Threads::Threads inputosm::inputosm)

add_executable(c_api c_api.c)
set_target_properties(c_api PROPERTIES LINKER_LANGUAGE C C_STANDARD 99)
target_link_libraries(c_api PRIVATE inputosm::inputosm)
