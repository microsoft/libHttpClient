cmake_minimum_required(VERSION 3.6)

get_filename_component(PATH_TO_ROOT "../.." ABSOLUTE)

project("libHttpClient.Linux")

include(CTest)
enable_testing()

option(HC_NOWEBSOCKETS "Disable WebSocket support" OFF)
option(HC_ENABLE_WEBSOCKET_COMPRESSION "Enable compression-capable WebSocket provider support" ON)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_SHARED_LINKER_FLAGS ${CMAKE_SHARED_LINKER_FLAGS} "-Wl,-z,now,--version-script=${PATH_TO_ROOT}/Build/libHttpClient.CMake/libHttpClientExports.txt")

###########################################
### Set up paths for source and include ###
###########################################

# Set final static libraries output folder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PATH_TO_ROOT}/Out/x64/Debug/libHttpClient.Linux)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PATH_TO_ROOT}/Out/x64/Release/libHttpClient.Linux)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${PATH_TO_ROOT}/Out/x64/Debug/libHttpClient.Linux)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${PATH_TO_ROOT}/Out/x64/Release/libHttpClient.Linux)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${PATH_TO_ROOT}/Out/x64/Debug/libHttpClient.Linux)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${PATH_TO_ROOT}/Out/x64/Release/libHttpClient.Linux)

set(LIBCRYPTO_BINARY_PATH
    ${PATH_TO_ROOT}/Out/x64/${CMAKE_BUILD_TYPE}/libcrypto.Linux/libcrypto.a
)
message(STATUS "LIBCRYPTO_BINARY_PATH: ${LIBCRYPTO_BINARY_PATH}")

set(LIBSSL_BINARY_PATH
    ${PATH_TO_ROOT}/Out/x64/${CMAKE_BUILD_TYPE}/libssl.Linux/libssl.a
)
message(STATUS "LIBSSL_BINARY_PATH: ${LIBSSL_BINARY_PATH}")

set(LIBCURL_BINARY_PATH
    ${PATH_TO_ROOT}/Out/x64/${CMAKE_BUILD_TYPE}/libcurl.Linux/libcurl.a
)
message(STATUS "LIBCURL_BINARY_PATH: ${LIBCURL_BINARY_PATH}")

include("../libHttpClient.CMake/GetCommonHCSourceFiles.cmake")
get_common_hc_source_files(
    PUBLIC_SOURCE_FILES
    HC_COMMON_SOURCE_FILES
    GLOBAL_SOURCE_FILES
    PLATFORM_SOURCE_FILES
    WEBSOCKET_SOURCE_FILES
    TASK_SOURCE_FILES
    MOCK_SOURCE_FILES
    HTTP_SOURCE_FILES
    LOGGER_SOURCE_FILES
    "${PATH_TO_ROOT}"
    )

set(COMMON_SOURCE_FILES
    "${PUBLIC_SOURCE_FILES}"
    "${HC_COMMON_SOURCE_FILES}"
    "${GLOBAL_SOURCE_FILES}"
    "${PLATFORM_SOURCE_FILES}"
    "${WEBSOCKET_SOURCE_FILES}"
    "${TASK_SOURCE_FILES}"
    "${MOCK_SOURCE_FILES}"
    "${HTTP_SOURCE_FILES}"
    "${LOGGER_SOURCE_FILES}"
    )

set(LINUX_SOURCE_FILES
    "${PATH_TO_ROOT}/Source/HTTP/Curl/CurlEasyRequest.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Curl/CurlEasyRequest.h"
    "${PATH_TO_ROOT}/Source/HTTP/Curl/CurlMulti.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Curl/CurlMulti.h"
    "${PATH_TO_ROOT}/Source/HTTP/Curl/CurlProvider.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Curl/CurlProvider.h"
    "${PATH_TO_ROOT}/Source/Platform/Generic/PlatformTrace_Generic.cpp"
    "${PATH_TO_ROOT}/Source/Platform/Linux/PlatformComponents_Linux.cpp"
    "${PATH_TO_ROOT}/Source/Task/ThreadPool_stl.cpp"
    "${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp"
    )

if (NOT HC_NOWEBSOCKETS)
    list(APPEND LINUX_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.cpp"
        "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.h"
        "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/x509_cert_utilities.hpp"
    )
endif()

if (NOT DEFINED HC_NOZLIB)
    set(ZLIB_SOURCE_FILES
       "${PATH_TO_ROOT}/External/zlib/adler32.c"
       "${PATH_TO_ROOT}/External/zlib/compress.c"
       "${PATH_TO_ROOT}/External/zlib/crc32.c"
       "${PATH_TO_ROOT}/External/zlib/deflate.c"
       "${PATH_TO_ROOT}/External/zlib/gzclose.c"
       "${PATH_TO_ROOT}/External/zlib/gzlib.c"
       "${PATH_TO_ROOT}/External/zlib/gzread.c"
       "${PATH_TO_ROOT}/External/zlib/gzwrite.c"
       "${PATH_TO_ROOT}/External/zlib/infback.c"
       "${PATH_TO_ROOT}/External/zlib/inffast.c"
       "${PATH_TO_ROOT}/External/zlib/inflate.c"
       "${PATH_TO_ROOT}/External/zlib/inftrees.c"
       "${PATH_TO_ROOT}/External/zlib/trees.c"
       "${PATH_TO_ROOT}/External/zlib/uncompr.c"
       "${PATH_TO_ROOT}/External/zlib/zutil.c"
       "${PATH_TO_ROOT}/External/zlib/contrib/minizip/unzip.c"
       "${PATH_TO_ROOT}/External/zlib/contrib/minizip/ioapi.c"
    )

    set(ZLIB_INCLUDE_DIRS
       "${PATH_TO_ROOT}/External/zlib"
       "${PATH_TO_ROOT}/External/zlib/contrib/minizip"
    )
endif()

 set(COMMON_INCLUDE_DIRS
    "${PATH_TO_ROOT}/Source"
    "${PATH_TO_ROOT}/Source/Common"
    "${PATH_TO_ROOT}/Source/HTTP"
    "${PATH_TO_ROOT}/Source/Logger"
    "${PATH_TO_ROOT}/Include"
    "${PATH_TO_ROOT}/Include/httpClient"
    "${PATH_TO_ROOT}/External/asio/asio/include"
    "${PATH_TO_ROOT}/External/openssl/include"
    "${PATH_TO_ROOT}/External/curl/include"
    "${PATH_TO_ROOT}/External/websocketpp"
    )

set(LINUX_INCLUDE_DIRS
    "${PATH_TO_ROOT}/External/opensslGeneratedHeaders/linux"
    )
if (BUILD_SHARED_LIBS)
    #########################
    ### Set up shared lib ###
    #########################

    add_library(
        "${PROJECT_NAME}"
        SHARED
        "${COMMON_SOURCE_FILES}"
        "${LINUX_SOURCE_FILES}"
        "${ZLIB_SOURCE_FILES}"
    )

    target_link_libraries("${PROJECT_NAME}"
        PRIVATE ${LIBCURL_BINARY_PATH}
        PRIVATE ${LIBSSL_BINARY_PATH}
        PRIVATE ${LIBCRYPTO_BINARY_PATH}
    )
else()
    #########################
    ### Set up static lib ###
    #########################

    add_library(
        "${PROJECT_NAME}"
        STATIC
        "${COMMON_SOURCE_FILES}"
        "${LINUX_SOURCE_FILES}"
        "${ZLIB_SOURCE_FILES}"
    )
endif()

target_include_directories(
    "${PROJECT_NAME}"
    PRIVATE
    "${COMMON_INCLUDE_DIRS}"
    "${LINUX_INCLUDE_DIRS}"
    "${ZLIB_INCLUDE_DIRS}"
)

if (NOT DEFINED HC_NOZLIB)
    target_compile_definitions("${PROJECT_NAME}" PRIVATE Z_HAVE_UNISTD_H=1)
endif()

# Turn on compiler warnings for all source files.
# For zlib, which is external, mask some of the unimportant warnings. This keeps our build output clean without
# adding friction to taking zlib updates. If new warnings show up in updates, they should be assessed for impact and
# disabled if not considered problematic.
target_compile_options("${PROJECT_NAME}" PRIVATE -Werror)
set_source_files_properties(${ZLIB_SOURCE_FILES} PROPERTIES COMPILE_OPTIONS "-Wno-implicit-function-declaration")

include("../libHttpClient.CMake/GetLibHCFlags.cmake")
get_libhc_flags(FLAGS FLAGS_DEBUG FLAGS_RELEASE)

include("../libHttpClient.CMake/TargetSetFlags.cmake")
target_set_flags(
    "${PROJECT_NAME}"
    "${FLAGS}"
    "${FLAGS_DEBUG}"
    "${FLAGS_RELEASE}"
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

if (HC_ENABLE_WEBSOCKET_COMPRESSION AND NOT HC_NOWEBSOCKETS)
    if (DEFINED HC_NOZLIB)
        message(FATAL_ERROR "HC_ENABLE_WEBSOCKET_COMPRESSION requires zlib support")
    endif()

    find_package(Threads REQUIRED)

    add_executable(
        "WebSocketCompressionTests.Linux"
        "${PATH_TO_ROOT}/Tests/WebSocketCompression/WebSocketCompressionTests.cpp"
        "${ZLIB_SOURCE_FILES}"
    )

    target_include_directories(
        "WebSocketCompressionTests.Linux"
        PRIVATE
        "${COMMON_INCLUDE_DIRS}"
        "${LINUX_INCLUDE_DIRS}"
        "${ZLIB_INCLUDE_DIRS}"
    )

    target_compile_definitions(
        "WebSocketCompressionTests.Linux"
        PRIVATE
        ASIO_STANDALONE
        Z_HAVE_UNISTD_H=1
    )

    target_link_libraries(
        "WebSocketCompressionTests.Linux"
        PRIVATE
        "${PROJECT_NAME}"
        Threads::Threads
        ${CMAKE_DL_LIBS} # Required for dlopen/dlsym referenced by linked libcrypto in some configurations
    )

    if (NOT BUILD_SHARED_LIBS)
        target_link_libraries(
            "WebSocketCompressionTests.Linux"
            PRIVATE
            "${LIBCURL_BINARY_PATH}"
            "${LIBSSL_BINARY_PATH}"
            "${LIBCRYPTO_BINARY_PATH}"
        )
    endif()

    target_set_flags(
        "WebSocketCompressionTests.Linux"
        "${FLAGS}"
        "${FLAGS_DEBUG}"
        "${FLAGS_RELEASE}"
    )

    add_test(
        NAME websocket-compression-linux
        COMMAND "WebSocketCompressionTests.Linux"
    )
    set_tests_properties(
        websocket-compression-linux
        PROPERTIES
        SKIP_RETURN_CODE 125
    )
endif()

##################################################
### TaskQueue delayed-callback starvation test ###
##################################################
# Standalone regression guard for the composite-queue delayed-callback
# starvation bug. Exercises the STL WaitTimer backend that the Windows test
# lanes cannot reach. Uses only the public XTaskQueue API + the standard
# library, so it just links the libHttpClient.Linux target.
find_package(Threads REQUIRED)

add_executable(
    "TaskQueueStarvationTests.Linux"
    "${PATH_TO_ROOT}/Tests/TaskQueueStarvation/TaskQueueStarvationRepro.cpp"
)

target_include_directories(
    "TaskQueueStarvationTests.Linux"
    PRIVATE
    "${COMMON_INCLUDE_DIRS}"
    "${LINUX_INCLUDE_DIRS}"
    "${PATH_TO_ROOT}/Tests/Shared"
)

target_link_libraries(
    "TaskQueueStarvationTests.Linux"
    PRIVATE
    "${PROJECT_NAME}"
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

if (NOT BUILD_SHARED_LIBS)
    target_link_libraries(
        "TaskQueueStarvationTests.Linux"
        PRIVATE
        "${LIBCURL_BINARY_PATH}"
        "${LIBSSL_BINARY_PATH}"
        "${LIBCRYPTO_BINARY_PATH}"
    )
endif()

target_set_flags(
    "TaskQueueStarvationTests.Linux"
    "${FLAGS}"
    "${FLAGS_DEBUG}"
    "${FLAGS_RELEASE}"
)

add_test(
    NAME taskqueue-starvation-linux
    COMMAND "TaskQueueStarvationTests.Linux"
)
# Label so CI can run every TaskQueue regression test by label (-L taskqueue)
# without having to opt each new test in by name.
set_tests_properties(
    taskqueue-starvation-linux
    PROPERTIES
    LABELS "taskqueue"
)

#################################################
### TaskQueue single-port poll strand test    ###
#################################################
# Standalone regression guard for the single-port delayed-callback strand bug.
# Exercises the STL WaitTimer backend that the Windows test lanes cannot reach.
# Uses only the public XTaskQueue API + the standard library, so it just links
# the libHttpClient.Linux target.
add_executable(
    "SingleQueuePollStrandTests.Linux"
    "${PATH_TO_ROOT}/Tests/TaskQueueStarvation/SingleQueuePollStrandRepro.cpp"
)

target_include_directories(
    "SingleQueuePollStrandTests.Linux"
    PRIVATE
    "${COMMON_INCLUDE_DIRS}"
    "${LINUX_INCLUDE_DIRS}"
    "${PATH_TO_ROOT}/Tests/Shared"
)

target_link_libraries(
    "SingleQueuePollStrandTests.Linux"
    PRIVATE
    "${PROJECT_NAME}"
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

if (NOT BUILD_SHARED_LIBS)
    target_link_libraries(
        "SingleQueuePollStrandTests.Linux"
        PRIVATE
        "${LIBCURL_BINARY_PATH}"
        "${LIBSSL_BINARY_PATH}"
        "${LIBCRYPTO_BINARY_PATH}"
    )
endif()

target_set_flags(
    "SingleQueuePollStrandTests.Linux"
    "${FLAGS}"
    "${FLAGS_DEBUG}"
    "${FLAGS_RELEASE}"
)

add_test(
    NAME taskqueue-poll-strand-linux
    COMMAND "SingleQueuePollStrandTests.Linux"
)
set_tests_properties(
    taskqueue-poll-strand-linux
    PROPERTIES
    LABELS "taskqueue"
)

export(TARGETS ${PROJECT_NAME} FILE ${PROJECT_NAME}Config.cmake)
