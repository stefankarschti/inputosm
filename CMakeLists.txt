cmake_minimum_required(VERSION 3.5 FATAL_ERROR)

project(inputosm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

# Toggle integration tests
option(INPUTOSM_INTEGRATION_TESTS "Build integration tests" ON)

# Inputosm deps
find_package(EXPAT REQUIRED)
find_package(ZLIB REQUIRED)

# Sources
set(SOURCES
    "include/inputosm/inputosm.h"
    "src/inputosm.cpp"
    "src/inputosmpbf.cpp"
    "src/inputosmxml.cpp"
    "src/timeutil.h"
    "src/timeutil.cpp"
)

set(LIBRARY_NAME ${PROJECT_NAME})
add_library(${LIBRARY_NAME} ${SOURCES})
add_library(${LIBRARY_NAME}::${LIBRARY_NAME} ALIAS ${LIBRARY_NAME})
target_include_directories(${LIBRARY_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(${LIBRARY_NAME} PRIVATE EXPAT::EXPAT ZLIB::ZLIB)
target_compile_features(inputosm PUBLIC "cxx_std_17")

# Tests
if(INPUTOSM_INTEGRATION_TESTS)
    add_subdirectory(test/integration)
endif()

# Configure installation files
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Useful paths
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

# Install actual files
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
