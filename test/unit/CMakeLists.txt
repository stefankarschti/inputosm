add_executable(thread_config_test thread_config_test.cpp)
add_executable(read_osm_test read_osm_test.cpp)
add_executable(read_osc_test read_osc_test.cpp)

target_link_libraries(thread_config_test PRIVATE inputosm::inputosm)
target_link_libraries(read_osm_test PRIVATE inputosm::inputosm)
target_link_libraries(read_osc_test PRIVATE inputosm::inputosm)

add_test(NAME thread_config COMMAND thread_config_test)
add_test(NAME read_osm COMMAND read_osm_test)
add_test(NAME read_osc COMMAND read_osc_test)

set_tests_properties(thread_config PROPERTIES LABELS unit)
set_tests_properties(read_osm PROPERTIES LABELS unit)
set_tests_properties(read_osc PROPERTIES LABELS unit)
