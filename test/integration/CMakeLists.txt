project(inputosm_integration_tests)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(inputosm_integration_test inputosm_integration_test.cpp)
target_link_libraries(inputosm_integration_test PRIVATE Threads::Threads inputosm::inputosm)
