cmake_minimum_required(VERSION 3.6)

get_filename_component(PATH_TO_ROOT "../../../.." ABSOLUTE)

project("libHttpClient.Android")

set(CMAKE_STATIC_LIBRARY_PREFIX "")

###########################################
### Set up paths for source and include ###
###########################################

include("../../GetCommonHCSourceFiles.cmake")
get_common_hc_source_files(
    PUBLIC_SOURCE_FILES
    HC_COMMON_SOURCE_FILES
    GLOBAL_SOURCE_FILES
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
    "${WEBSOCKET_SOURCE_FILES}"
    "${TASK_SOURCE_FILES}"
    "${MOCK_SOURCE_FILES}"
    "${HTTP_SOURCE_FILES}"
    "${LOGGER_SOURCE_FILES}"
    )

set(ANDROID_SOURCE_FILES
    "${PATH_TO_ROOT}/Include/httpClient/async_jvm.h"
    "${PATH_TO_ROOT}/Source/Common/Android/utils_android.cpp"
    "${PATH_TO_ROOT}/Source/Common/Android/utils_android.h"
    "${PATH_TO_ROOT}/Source/HTTP/Android/http_android.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Android/http_android.h"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.h"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_platform_context.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_platform_context.h"
    "${PATH_TO_ROOT}/Source/Logger/Android/android_logger.cpp"
    "${PATH_TO_ROOT}/Source/Task/ThreadPool_stl.cpp"
    "${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp"
    "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.cpp"
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
    "${PATH_TO_ROOT}/External/websocketpp"
    )

set(ANDROID_INCLUDE_DIRS
    "${PATH_TO_ROOT}/External/opensslGeneratedHeaders/android"
    )

#########################
### Set up static lib ###
#########################

add_library(
    "${PROJECT_NAME}"
    STATIC
    "${COMMON_SOURCE_FILES}"
    "${ANDROID_SOURCE_FILES}"
)

target_include_directories(
    "${PROJECT_NAME}"
    PRIVATE
    "${COMMON_INCLUDE_DIRS}"
    "${ANDROID_INCLUDE_DIRS}"
)

include("GetLibHCFlags.cmake")
get_libhc_flags(FLAGS FLAGS_DEBUG FLAGS_RELEASE)

include("../TargetSetFlags.cmake")
target_set_flags(
    "${PROJECT_NAME}"
    "${FLAGS}"
    "${FLAGS_DEBUG}"
    "${FLAGS_RELEASE}"
)
