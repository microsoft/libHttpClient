cmake_minimum_required(VERSION 3.6)

get_filename_component(PATH_TO_ROOT "../.." ABSOLUTE)

project("libHttpClient.Linux")

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
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

# BINARY_DIR is a temp folder used by cmake itself.
# Binary folder can be remove freely.
# See more details: https://cmake.org/cmake/help/v3.4/command/add_subdirectory.html
set(BINARY_DIR ${PATH_TO_ROOT}/Int/CMake/libHttpClient.Linux)

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
    "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.cpp"
    "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.h"
    "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/x509_cert_utilities.hpp"
    )

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

#########################
### Set up shared lib ###
#########################

add_library(
    "${PROJECT_NAME}"
    SHARED
    "${COMMON_SOURCE_FILES}"
    "${LINUX_SOURCE_FILES}"
)

target_link_libraries("${PROJECT_NAME}" 
    PRIVATE ${LIBCURL_BINARY_PATH}
    PRIVATE ${LIBSSL_BINARY_PATH}
    PRIVATE ${LIBCRYPTO_BINARY_PATH} 
)

target_include_directories(
    "${PROJECT_NAME}"
    PRIVATE
    "${COMMON_INCLUDE_DIRS}"
    "${LINUX_INCLUDE_DIRS}"
)

include("../libHttpClient.CMake/GetLibHCFlags.cmake")
get_libhc_flags(FLAGS FLAGS_DEBUG FLAGS_RELEASE)

include("../libHttpClient.CMake/TargetSetFlags.cmake")
target_set_flags(
    "${PROJECT_NAME}"
    "${FLAGS}"
    "${FLAGS_DEBUG}"
    "${FLAGS_RELEASE}"
)

export(TARGETS ${PROJECT_NAME} FILE ${PROJECT_NAME}Config.cmake)