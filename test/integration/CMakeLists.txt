project(inputosm_integration_tests)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(count_all count_all.cpp)
target_link_libraries(count_all PRIVATE Threads::Threads inputosm::inputosm fmt::fmt)

add_executable(statistics statistics.cpp)
target_link_libraries(statistics PRIVATE Threads::Threads inputosm::inputosm fmt::fmt)

add_executable(extract_ferries extract_ferries.cpp)
target_link_libraries(extract_ferries PRIVATE Threads::Threads inputosm::inputosm fmt::fmt)

add_executable(export_csv export_csv.cpp)
target_link_libraries(export_csv PRIVATE Threads::Threads inputosm::inputosm fmt::fmt)

add_executable(lat_stat lat_stat.cpp)
target_link_libraries(lat_stat PRIVATE Threads::Threads inputosm::inputosm fmt::fmt)

add_executable(export_db export_db.cpp)
target_link_libraries(export_db PRIVATE Threads::Threads inputosm::inputosm fmt::fmt)

add_executable(custom_log custom_log.cpp)
target_link_libraries(custom_log PRIVATE Threads::Threads inputosm::inputosm fmt::fmt)

add_executable(count_blocks count_blocks.cpp)
target_link_libraries(count_blocks PRIVATE inputosm::inputosm fmt::fmt)

add_executable(read_block read_block.cpp)
target_link_libraries(read_block PRIVATE inputosm::inputosm fmt::fmt)

if(BUILD_TESTING)
    add_test(NAME output_locale
        COMMAND ${CMAKE_COMMAND}
            -DCOUNT_ALL=$<TARGET_FILE:count_all>
            -DCOUNT_BLOCKS=$<TARGET_FILE:count_blocks>
            -DSTATISTICS=$<TARGET_FILE:statistics>
            -DLAT_STAT=$<TARGET_FILE:lat_stat>
            -DEXTRACT_FERRIES=$<TARGET_FILE:extract_ferries>
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/../unit/data/comprehensive.osm.pbf
            -P ${CMAKE_CURRENT_SOURCE_DIR}/output_locale_test.cmake
    )
    set_tests_properties(output_locale PROPERTIES LABELS integration TIMEOUT 30)
endif()
