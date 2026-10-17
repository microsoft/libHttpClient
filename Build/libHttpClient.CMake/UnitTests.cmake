cmake_minimum_required(VERSION 3.6)

# Builds the shared unit tests (Tests/UnitTests/Tests/*.cpp) as a standalone executable using the
# portable header-only runner (Tests/UnitTests/Support/Portable). Like the Windows TE/TAEF unit-test
# projects (Build/libHttpClient.UnitTest/libHttpClient.UnitTest.vcxitems), the library sources are
# compiled directly into the test executable against the Generic platform (no real HTTP/WebSocket
# provider, so no curl/OpenSSL dependency) with HC_UNITTEST_API defined. The shipped library
# targets are not affected.
#
# Every Tests/UnitTests/Tests/*.cpp is picked up automatically; list files that cannot build on
# this platform in HC_UNITTEST_EXCLUDED_SOURCES below (with the reason).
#
# Registers the executable with ctest as "libHttpClient.UnitTests" (label "unit"). Run a subset
# directly with: libHttpClient.UnitTests --filter=HttpTests::*,-*Slow*   (or --list)
function(HC_ADD_UNIT_TESTS PATH_TO_ROOT FLAGS FLAGS_DEBUG FLAGS_RELEASE)
    include("${PATH_TO_ROOT}/Build/libHttpClient.CMake/GetCommonHCSourceFiles.cmake")
    get_common_hc_source_files(
        UT_PUBLIC_SOURCE_FILES
        UT_COMMON_SOURCE_FILES
        UT_GLOBAL_SOURCE_FILES
        UT_PLATFORM_SOURCE_FILES
        UT_WEBSOCKET_SOURCE_FILES
        UT_TASK_SOURCE_FILES
        UT_MOCK_SOURCE_FILES
        UT_HTTP_SOURCE_FILES
        UT_LOGGER_SOURCE_FILES
        "${PATH_TO_ROOT}"
        )

    set(HC_UNITTEST_EXCLUDED_SOURCES
        # Tests the WinHTTP proxy helper (Source/HTTP/WinHttp), which only exists on Windows.
        "ProxyTests.cpp"
        )

    if (CMAKE_VERSION VERSION_LESS 3.12)
        file(GLOB UT_TEST_SOURCE_FILES "${PATH_TO_ROOT}/Tests/UnitTests/Tests/*.cpp")
    else()
        file(GLOB UT_TEST_SOURCE_FILES CONFIGURE_DEPENDS "${PATH_TO_ROOT}/Tests/UnitTests/Tests/*.cpp")
    endif()
    foreach(excluded ${HC_UNITTEST_EXCLUDED_SOURCES})
        list(REMOVE_ITEM UT_TEST_SOURCE_FILES "${PATH_TO_ROOT}/Tests/UnitTests/Tests/${excluded}")
    endforeach()
    list(SORT UT_TEST_SOURCE_FILES)

    set(UT_TARGET "libHttpClient.UnitTests")
    add_executable(
        "${UT_TARGET}"
        ${UT_COMMON_SOURCE_FILES}
        ${UT_GLOBAL_SOURCE_FILES}
        ${UT_PLATFORM_SOURCE_FILES}
        ${UT_WEBSOCKET_SOURCE_FILES}
        ${UT_TASK_SOURCE_FILES}
        ${UT_MOCK_SOURCE_FILES}
        ${UT_HTTP_SOURCE_FILES}
        ${UT_LOGGER_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/Platform/Generic/PlatformComponents_Generic.cpp"
        "${PATH_TO_ROOT}/Source/Platform/Generic/PlatformTrace_Generic.cpp"
        "${PATH_TO_ROOT}/Source/Task/ThreadPool_stl.cpp"
        "${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp"
        "${PATH_TO_ROOT}/Tests/UnitTests/Support/Portable/UnitTestMain_Portable.cpp"
        "${PATH_TO_ROOT}/Tests/UnitTests/Support/TestEnvironment.cpp"
        ${UT_TEST_SOURCE_FILES}
        )

    target_include_directories(
        "${UT_TARGET}"
        PRIVATE
        "${PATH_TO_ROOT}/Source"
        "${PATH_TO_ROOT}/Source/Common"
        "${PATH_TO_ROOT}/Source/HTTP"
        "${PATH_TO_ROOT}/Source/Logger"
        "${PATH_TO_ROOT}/Source/Task"
        "${PATH_TO_ROOT}/Include"
        "${PATH_TO_ROOT}/Include/httpClient"
        "${PATH_TO_ROOT}/Tests/UnitTests/Support"
        "${PATH_TO_ROOT}/Tests/UnitTests"
        "${PATH_TO_ROOT}/Tests/Shared"
        )

    # Same defines as libHttpClient.UnitTest.vcxitems + the TE project. Like those projects (and the
    # shipping libraries), zlib is compiled in by default so CompressionTests runs; configure with
    # -DHC_NOZLIB=1 to build the no-zlib variant instead.
    target_compile_definitions(
        "${UT_TARGET}"
        PRIVATE
        HC_UNITTEST_API
        UNITTEST_PORTABLE
        # The sources are compiled straight into the test binary (no framework); on Apple async.h
        # otherwise includes <httpClient/XAsync.h> from the framework. Harmless elsewhere.
        HC_LINK_STATIC=1
        )

    if (DEFINED HC_NOZLIB)
        target_compile_definitions("${UT_TARGET}" PRIVATE HC_NOZLIB=1)
    else()
        set(UT_ZLIB_DIR "${PATH_TO_ROOT}/External/zlib")
        set(UT_ZLIB_SOURCE_FILES)
        foreach(zlibSource adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil)
            list(APPEND UT_ZLIB_SOURCE_FILES "${UT_ZLIB_DIR}/${zlibSource}.c")
        endforeach()
        target_sources("${UT_TARGET}" PRIVATE ${UT_ZLIB_SOURCE_FILES})
        target_include_directories("${UT_TARGET}" PRIVATE "${UT_ZLIB_DIR}")
        target_compile_definitions("${UT_TARGET}" PRIVATE Z_HAVE_UNISTD_H=1)
        # Third-party C: same warning relaxation as the Linux library build.
        set_source_files_properties(${UT_ZLIB_SOURCE_FILES} PROPERTIES COMPILE_OPTIONS "-Wno-implicit-function-declaration")
    endif()

    find_package(Threads REQUIRED)
    target_link_libraries("${UT_TARGET}" PRIVATE Threads::Threads)

    include("${PATH_TO_ROOT}/Build/libHttpClient.CMake/TargetSetFlags.cmake")
    # Like the Windows unit-test projects, keep the default (verbose) trace build level in every
    # configuration: TraceTests emits HC_TRACE_INFORMATION and expects it to be compiled in.
    list(FILTER FLAGS_DEBUG EXCLUDE REGEX "HC_TRACE_BUILD_LEVEL")
    list(FILTER FLAGS_RELEASE EXCLUDE REGEX "HC_TRACE_BUILD_LEVEL")
    target_set_flags("${UT_TARGET}" "${FLAGS}" "${FLAGS_DEBUG}" "${FLAGS_RELEASE}")
    if (NOT CMAKE_CXX_STANDARD)
        set_target_properties("${UT_TARGET}" PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    endif()

    add_test(NAME libHttpClient.UnitTests COMMAND "${UT_TARGET}")
    set_tests_properties(libHttpClient.UnitTests PROPERTIES LABELS "unit" TIMEOUT 1200)
endfunction()
