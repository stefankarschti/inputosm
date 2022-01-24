cmake_minimum_required(VERSION 3.5 FATAL_ERROR)
project(inputosm VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

set(
    SOURCES
        "include/inputosm/inputosm.h"
        "src/inputosm.cpp"
        "src/inputosmpbf.cpp"
        "src/inputosmxml.cpp"
        "src/timeutil.h"
        "src/timeutil.cpp"
)

#add_definitions("-D_FILE_OFFSET_BITS=64")

add_library(inputosm ${SOURCES})
set(LIBRARY_NAME
    inputosm
)
target_include_directories(${LIBRARY_NAME} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/${LIBRARY_NAME}>
    $<INSTALL_INTERFACE:include/${LIBRARY_NAME}>
)
target_include_directories(${LIBRARY_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(inputosm PUBLIC "cxx_std_17")
install(TARGETS inputosm DESTINATION lib)
install(DIRECTORY include/inputosm DESTINATION include)

#add_subdirectory("test")