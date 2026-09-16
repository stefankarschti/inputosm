add_executable(thread_config_test thread_config_test.cpp)
add_executable(read_osm_test read_osm_test.cpp)
add_executable(read_osc_test read_osc_test.cpp)

target_link_libraries(thread_config_test PRIVATE inputosm::inputosm fmt::fmt)
target_link_libraries(read_osm_test PRIVATE inputosm::inputosm fmt::fmt)
target_link_libraries(read_osc_test PRIVATE inputosm::inputosm fmt::fmt)

add_test(NAME thread_config COMMAND thread_config_test)
add_test(NAME read_osm COMMAND read_osm_test)
add_test(NAME read_osc COMMAND read_osc_test)

set_tests_properties(thread_config PROPERTIES LABELS unit)
set_tests_properties(read_osm PROPERTIES LABELS unit)
set_tests_properties(read_osc PROPERTIES LABELS unit)

add_executable(pbf_block_test pbf_block_test.cpp)
target_link_libraries(pbf_block_test PRIVATE inputosm::inputosm ZLIB::ZLIB Threads::Threads fmt::fmt)
add_test(NAME pbf_block COMMAND pbf_block_test)
set_tests_properties(pbf_block PROPERTIES LABELS unit TIMEOUT 60)

add_executable(output_format_test output_format_test.cpp)
target_include_directories(output_format_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_link_libraries(output_format_test PRIVATE inputosm::inputosm fmt::fmt)
add_test(NAME output_format COMMAND output_format_test)
set_tests_properties(output_format PROPERTIES LABELS unit ENVIRONMENT "LC_ALL=inputosm_missing_locale")
