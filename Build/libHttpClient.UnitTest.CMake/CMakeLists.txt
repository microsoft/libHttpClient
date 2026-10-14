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

#################################################################################################
### macOS end-to-end tests (Tests/E2E, ctest label "e2e")                                     ###
#################################################################################################
# Opt-in (macOS only): builds a static libHttpClient with the real Apple providers -- NSURLSession
# for HTTP and websocketpp (+ OpenSSL, + permessage-deflate) for WebSockets, mirroring the
# libHttpClient_macOS Xcode target -- and the Tests/E2E suite against it. The suite starts the
# Python 3 loopback server (Tests/E2E/server) itself. OpenSSL comes from the host, e.g.
#
#   cmake -S Build/libHttpClient.UnitTest.CMake -B <dir> -DCMAKE_BUILD_TYPE=Debug \
#         -DHC_BUILD_E2E_TESTS=ON -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
#   cmake --build <dir> --target libHttpClient.E2E && (cd <dir> && ctest --output-on-failure -L e2e)
#
# Validated only in the macOS CI legs (no local macOS build); see macos-build.yml.
option(HC_BUILD_E2E_TESTS "macOS only: build the Tests/E2E suite against an NSURLSession + websocketpp build (ctest label 'e2e')" OFF)
if (HC_BUILD_E2E_TESTS)
    if (NOT APPLE)
        message(FATAL_ERROR "HC_BUILD_E2E_TESTS in this project is for macOS. On Linux, use Build/libHttpClient.Linux (HC_BUILD_E2E_TESTS is ON there).")
    endif()
    if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)
        enable_language(OBJCXX)
    endif()
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)

    include("${PATH_TO_ROOT}/Build/libHttpClient.CMake/GetCommonHCSourceFiles.cmake")
    get_common_hc_source_files(
        PUBLIC_SOURCE_FILES HC_COMMON_SOURCE_FILES GLOBAL_SOURCE_FILES PLATFORM_SOURCE_FILES
        WEBSOCKET_SOURCE_FILES TASK_SOURCE_FILES MOCK_SOURCE_FILES HTTP_SOURCE_FILES LOGGER_SOURCE_FILES
        "${PATH_TO_ROOT}")

    set(HC_APPLE_MM_SOURCES
        "${PATH_TO_ROOT}/Source/Common/Apple/utils_apple.mm"
        "${PATH_TO_ROOT}/Source/HTTP/Apple/http_apple.mm"
        "${PATH_TO_ROOT}/Source/HTTP/Apple/request_body_stream.mm"
        "${PATH_TO_ROOT}/Source/HTTP/Apple/session_delegate.mm")
    set(HC_APPLE_ZLIB_SOURCES)
    foreach (zfile adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil)
        list(APPEND HC_APPLE_ZLIB_SOURCES "${PATH_TO_ROOT}/External/zlib/${zfile}.c")
    endforeach()

    add_library(libHttpClient.Apple.E2E STATIC
        ${PUBLIC_SOURCE_FILES} ${HC_COMMON_SOURCE_FILES} ${GLOBAL_SOURCE_FILES} ${PLATFORM_SOURCE_FILES}
        ${WEBSOCKET_SOURCE_FILES} ${TASK_SOURCE_FILES} ${MOCK_SOURCE_FILES} ${HTTP_SOURCE_FILES} ${LOGGER_SOURCE_FILES}
        ${HC_APPLE_MM_SOURCES}
        "${PATH_TO_ROOT}/Source/Platform/Apple/PlatformComponents_Apple.cpp"
        "${PATH_TO_ROOT}/Source/Platform/Apple/PlatformTrace_Apple.cpp"
        "${PATH_TO_ROOT}/Source/Task/ThreadPool_stl.cpp"
        "${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp"
        "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.cpp"
        ${HC_APPLE_ZLIB_SOURCES})

    target_include_directories(libHttpClient.Apple.E2E PRIVATE
        "${PATH_TO_ROOT}/Include"
        "${PATH_TO_ROOT}/Source"
        "${PATH_TO_ROOT}/Source/Common"
        "${PATH_TO_ROOT}/Source/Global"
        "${PATH_TO_ROOT}/Source/HTTP"
        "${PATH_TO_ROOT}/Source/HTTP/Apple"
        "${PATH_TO_ROOT}/Source/Logger"
        "${PATH_TO_ROOT}/Source/Platform"
        "${PATH_TO_ROOT}/Source/Task"
        "${PATH_TO_ROOT}/Source/WebSocket"
        "${PATH_TO_ROOT}/External/zlib"
        "${PATH_TO_ROOT}/External/zlib/contrib/minizip"
        "${PATH_TO_ROOT}/External/websocketpp"
        "${PATH_TO_ROOT}/External/asio/asio/include"
        "${OPENSSL_INCLUDE_DIR}")
    target_compile_definitions(libHttpClient.Apple.E2E PRIVATE
        HC_LINK_STATIC=1
        HC_ENABLE_WEBSOCKET_COMPRESSION=1
        Z_HAVE_UNISTD_H=1
        $<IF:$<CONFIG:Debug>,HC_TRACE_BUILD_LEVEL=HC_PRIVATE_TRACE_LEVEL_VERBOSE,HC_TRACE_BUILD_LEVEL=HC_PRIVATE_TRACE_LEVEL_IMPORTANT>)
    target_compile_options(libHttpClient.Apple.E2E PRIVATE ${HC_UT_FLAGS})
    set_source_files_properties(${HC_APPLE_MM_SOURCES} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(${HC_APPLE_ZLIB_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-macro-redefined;-Wno-implicit-function-declaration")
    target_link_libraries(libHttpClient.Apple.E2E PUBLIC
        OpenSSL::SSL OpenSSL::Crypto
        "-framework Foundation" "-framework CFNetwork" "-framework Security"
        Threads::Threads)

    include("${PATH_TO_ROOT}/Build/libHttpClient.CMake/E2ETests.cmake")
    hc_add_e2e_tests(
        ROOT "${PATH_TO_ROOT}"
        LIBRARIES libHttpClient.Apple.E2E
        FLAGS ${HC_UT_FLAGS}
        FLAGS_DEBUG ${HC_UT_FLAGS_DEBUG}
        FLAGS_RELEASE ${HC_UT_FLAGS_RELEASE})
endif()
