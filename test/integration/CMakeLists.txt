project(inputosm_integration_tests)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(inputosm_count_all inputosm_count_all.cpp)
target_link_libraries(inputosm_count_all PRIVATE Threads::Threads inputosm::inputosm)

add_executable(inputosm_count_all2 inputosm_count_all2.cpp)
target_link_libraries(inputosm_count_all2 PRIVATE Threads::Threads inputosm::inputosm)

add_executable(inputosm_extract_ferries inputosm_extract_ferries.cpp)
target_link_libraries(inputosm_extract_ferries PRIVATE Threads::Threads inputosm::inputosm)
