cmake_minimum_required(VERSION 3.13)

# Host build of the portable unit tests only (libHttpClient.UnitTests, ctest label "unit").
#
# Used by the macOS CI legs (Utilities/Pipelines/Tasks/macos-build.yml): the shipping macOS
# libraries are built with Xcode, which has no ctest integration, so this small project builds just
# the unit-test executable with the host compiler. It works on any Unix-like host (Linux too), needs
# no curl/OpenSSL (the tests run against the Generic platform, like the Windows TE project) and
# compiles zlib from External/ so CompressionTests runs. The test target itself is defined in
# Build/libHttpClient.CMake/UnitTests.cmake, shared with the Linux CMake build.
#
#   cmake -S Build/libHttpClient.UnitTest.CMake -B <dir> -DCMAKE_BUILD_TYPE=Debug|Release
#   cmake --build <dir> --parallel
#   (cd <dir> && ctest --output-on-failure -L unit)

project("libHttpClient.UnitTest.Host" C CXX)

get_filename_component(PATH_TO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Restore third-party sources (zlib) into External/ if missing or out of date (see Build/vcpkg).
include("${PATH_TO_ROOT}/Build/vcpkg/AutoRestore.cmake")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

option(HC_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if (HC_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# The sources carry MSVC "#pragma warning" lines and "#pragma once" in .cpp-included headers.
set(HC_UT_FLAGS "-Wno-unknown-pragmas" "-Wno-pragma-once-outside-header")
set(HC_UT_FLAGS_DEBUG "-O0")
set(HC_UT_FLAGS_RELEASE "-O2")

include("${PATH_TO_ROOT}/Build/libHttpClient.CMake/UnitTests.cmake")
hc_add_unit_tests("${PATH_TO_ROOT}" "${HC_UT_FLAGS}" "${HC_UT_FLAGS_DEBUG}" "${HC_UT_FLAGS_RELEASE}")
