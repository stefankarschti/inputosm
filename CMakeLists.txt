cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(inputosm VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

# Get the specified fmt release.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(FMT_DOC OFF)
set(FMT_TEST OFF)
set(FMT_MODULE OFF)
set(FMT_INSTALL ON)
set(FMT_SYSTEM_HEADERS ON)
FetchContent_Declare(fmt
    URL https://codeload.github.com/fmtlib/fmt/tar.gz/refs/tags/12.2.0
    URL_HASH SHA256=8b852bb5aa6e7d8564f9e81394055395dd1d1936d38dfd3a17792a02bebd7af0
)
FetchContent_MakeAvailable(fmt)

# Set the build options.
option(INPUTOSM_INTEGRATION_TESTS "Build integration tests" ON)
option(INPUTOSM_BENCHMARKS "Build PBF benchmarks" OFF)
option(INPUTOSM_BENCH_GENERIC_HEADERS "Use generic BlobHeader parsing for benchmark comparison" OFF)
option(INPUTOSM_BENCH_INDEPENDENT_MAPS "Use separate file mappings for benchmark comparison" OFF)
option(WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy checks" ON)

# Find the dependencies for inputosm.
find_package(EXPAT REQUIRED)
find_package(Threads REQUIRED)
add_subdirectory(cmake/libdeflate)

if(WARNINGS_AS_ERRORS)
    if(MSVC)
        add_compile_options(/W4 /WX)
    else()
        add_compile_options(-Wall -Wextra -Werror)
    endif()
endif()

if (ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_COMMAND NAMES clang-tidy)
    if(NOT CLANG_TIDY_COMMAND)
        message(FATAL_ERROR "ENABLE_CLANG_TIDY is ON but clang-tidy is not found!")
    else()
        message(STATUS "Found clang-tidy at ${CLANG_TIDY_COMMAND}")
    endif()
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND};--extra-arg=-Wno-unknown-warning-option;--extra-arg=-Wno-pragmas")
endif()

# Specify the source files.
set(SOURCES
    "include/inputosm/inputosm.h"
    "src/inputosm.cpp"
    "src/inputosmpbf.cpp"
    "src/inputosmxml.cpp"
    "src/timeutil.h"
    "src/timeutil.cpp"
    "src/inputosmlog.h"
    "src/inputosmlog.cpp"
)

set(LIBRARY_NAME ${PROJECT_NAME})
add_library(${LIBRARY_NAME} ${SOURCES})
add_library(${LIBRARY_NAME}::${LIBRARY_NAME} ALIAS ${LIBRARY_NAME})
if(INPUTOSM_BENCH_GENERIC_HEADERS)
    target_compile_definitions(${LIBRARY_NAME} PRIVATE INPUTOSM_BENCH_GENERIC_HEADERS)
endif()
if(INPUTOSM_BENCH_INDEPENDENT_MAPS)
    target_compile_definitions(${LIBRARY_NAME} PRIVATE INPUTOSM_BENCH_INDEPENDENT_MAPS)
endif()
target_include_directories(${LIBRARY_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(${LIBRARY_NAME} PRIVATE EXPAT::EXPAT libdeflate::libdeflate_static Threads::Threads fmt::fmt)
target_compile_features(${LIBRARY_NAME} PUBLIC cxx_std_20)

if(BUILD_TESTING)
    add_subdirectory(test/unit)
endif()

# Add the integration tests.
if(INPUTOSM_INTEGRATION_TESTS)
    add_subdirectory(test/integration)
endif()

if(INPUTOSM_BENCHMARKS)
    add_subdirectory(test/benchmark)
endif()

# Configure the installation files.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Set the paths for the package files.
set(${LIBRARY_NAME}_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR}/cmake)
set(${LIBRARY_NAME}_INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/cmake/${LIBRARY_NAME})
set(${LIBRARY_NAME}_GENERATED_PATH "${CMAKE_CURRENT_BINARY_DIR}/generated")

write_basic_package_version_file(${${LIBRARY_NAME}_GENERATED_PATH}/${LIBRARY_NAME}-config-version.cmake
    COMPATIBILITY ExactVersion
)

configure_package_config_file(${${LIBRARY_NAME}_CMAKE_DIR}/${LIBRARY_NAME}-config.cmake.in
    ${${LIBRARY_NAME}_GENERATED_PATH}/${LIBRARY_NAME}-config.cmake
    INSTALL_DESTINATION ${${LIBRARY_NAME}_INSTALL_CONFIGDIR}
)

configure_file(${${LIBRARY_NAME}_CMAKE_DIR}/version.h.in
    ${${LIBRARY_NAME}_GENERATED_PATH}/include/${LIBRARY_NAME}/version.h @ONLY
)

configure_file(${${LIBRARY_NAME}_CMAKE_DIR}/${LIBRARY_NAME}.pc.in
    ${${LIBRARY_NAME}_GENERATED_PATH}/${LIBRARY_NAME}.pc @ONLY)

# Install the package files.
install(
    DIRECTORY 
        include/${LIBRARY_NAME}
        ${${LIBRARY_NAME}_GENERATED_PATH}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    TARGETS ${LIBRARY_NAME}
    EXPORT ${LIBRARY_NAME}-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
    FILES
        ${${LIBRARY_NAME}_GENERATED_PATH}/${LIBRARY_NAME}-config.cmake
        ${${LIBRARY_NAME}_GENERATED_PATH}/${LIBRARY_NAME}-config-version.cmake
    DESTINATION ${${LIBRARY_NAME}_INSTALL_CONFIGDIR}
)

install(
    FILES 
        ${${LIBRARY_NAME}_GENERATED_PATH}/${LIBRARY_NAME}.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig/
)

install(
    EXPORT ${LIBRARY_NAME}-targets
    NAMESPACE ${LIBRARY_NAME}::
    DESTINATION ${${LIBRARY_NAME}_INSTALL_CONFIGDIR}
    COMPONENT dev
)
