cmake_minimum_required(VERSION 3.6)

# The libHttpClient end-to-end tests (Tests/E2E): the same test sources the Windows
# libHttpClient.E2E.Win32 project builds, run against a real platform build of the library (curl +
# websocketpp on Linux) and the Python 3 stdlib-only loopback server in Tests/E2E/server, which the
# executable starts itself. Registered with ctest under the label "e2e"; needs python3 on PATH at
# test time (and the openssl CLI for the HTTPS tests, which otherwise skip).
#
#   hc_add_e2e_tests(
#       ROOT <repo root>
#       LIBRARIES <libHttpClient target> [<extra link libraries>...]
#       [FLAGS <compile flags>...] [FLAGS_DEBUG ...] [FLAGS_RELEASE ...])
function(hc_add_e2e_tests)
    cmake_parse_arguments(E2E "" "ROOT" "LIBRARIES;FLAGS;FLAGS_DEBUG;FLAGS_RELEASE" ${ARGN})

    set(E2E_DIR "${E2E_ROOT}/Tests/E2E")
    set(E2E_TARGET "libHttpClient.E2E")

    find_package(Threads REQUIRED)

    add_executable(
        "${E2E_TARGET}"
        "${E2E_DIR}/E2EHelpers.cpp"
        "${E2E_DIR}/E2EPlatform.cpp"
        "${E2E_DIR}/E2ETestMain.cpp"
        "${E2E_DIR}/HttpE2ETests.cpp"
        "${E2E_DIR}/TestServer.cpp"
        "${E2E_DIR}/WebSocketE2ETests.cpp"
    )

    set_target_properties("${E2E_TARGET}" PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        # -rdynamic, so the runner's crash handler can print symbol names in its backtrace.
        ENABLE_EXPORTS ON)
    target_include_directories("${E2E_TARGET}" PRIVATE "${E2E_ROOT}/Include")
    target_link_libraries("${E2E_TARGET}" PRIVATE ${E2E_LIBRARIES} Threads::Threads ${CMAKE_DL_LIBS})

    target_compile_options("${E2E_TARGET}" PRIVATE ${E2E_FLAGS})
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options("${E2E_TARGET}" PRIVATE ${E2E_FLAGS_DEBUG})
    elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options("${E2E_TARGET}" PRIVATE ${E2E_FLAGS_RELEASE})
    endif()

    # The exe finds the server by walking up from its own location; pass it explicitly so the
    # test also works from out-of-tree build directories.
    add_test(
        NAME libHttpClient.E2E
        COMMAND "${E2E_TARGET}" --server-script "${E2E_DIR}/server/lhc_test_server.py"
    )
    # The suite takes well under a minute; the per-test watchdog (120 s) turns hangs into failures.
    set_tests_properties(libHttpClient.E2E PROPERTIES LABELS "e2e" TIMEOUT 900)
endfunction()
