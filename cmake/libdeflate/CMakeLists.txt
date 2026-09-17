if(TARGET libdeflate::libdeflate_static)
    return()
endif()

include(FetchContent)
include(GNUInstallDirs)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if(BUILD_SHARED_LIBS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
set(LIBDEFLATE_BUILD_STATIC_LIB ON)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF)
set(LIBDEFLATE_BUILD_GZIP OFF)
set(LIBDEFLATE_BUILD_TESTS OFF)
set(LIBDEFLATE_INSTALL ON)

FetchContent_Declare(libdeflate
    URL https://codeload.github.com/ebiggers/libdeflate/tar.gz/refs/tags/v1.26
    URL_HASH SHA256=bba03fffc5538576213675ce6968fcff6ce2e67d82e4d5febea2d05f9f13cf85)
FetchContent_MakeAvailable(libdeflate)
install(FILES "${libdeflate_SOURCE_DIR}/COPYING"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/libdeflate")
