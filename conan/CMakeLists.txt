# the conan cmake generator needs at least 3.15
cmake_minimum_required(VERSION 3.16)

project(inputosm-conan)

# conan needs a build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type for inputosm" FORCE)
  message(STATUS "No build type selected, default to '${CMAKE_BUILD_TYPE}'")
endif()

# look for conan
find_program(CONAN_EXE conan)
if(NOT CONAN_EXE)
    message(FATAL_ERROR "Conan executable was not found. Please install using `[sudo] pip3 install conan`")
endif()

include(conan.cmake)

# setup conan
conan_add_remote(NAME conancenter
    INDEX 0
    URL https://center.conan.io)

conan_cmake_autodetect(ENV_SETTINGS)

conan_cmake_install(
    PATH_OR_REFERENCE ${CMAKE_SOURCE_DIR}
    SETTINGS ${ENV_SETTINGS}
    BUILD missing)

# generated using the conanfile.txt in this directory
include(${CMAKE_BINARY_DIR}/conan_paths.cmake)

# has to be called from the main cml
enable_testing()

# add the actual project
add_subdirectory(${PROJECT_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR}/inputosm)
