project(inputosm_integration_tests)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(inputosm_count_all inputosm_count_all.cpp)
target_link_libraries(inputosm_count_all PRIVATE Threads::Threads inputosm::inputosm)

add_executable(inputosm_count_all2 inputosm_count_all2.cpp)
target_link_libraries(inputosm_count_all2 PRIVATE Threads::Threads inputosm::inputosm)

add_executable(extract_ferries extract_ferries.cpp)
target_link_libraries(extract_ferries PRIVATE Threads::Threads inputosm::inputosm)

add_executable(export_csv export_csv.cpp)
target_link_libraries(export_csv PRIVATE Threads::Threads inputosm::inputosm)
