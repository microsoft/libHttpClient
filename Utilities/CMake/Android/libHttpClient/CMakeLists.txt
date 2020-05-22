cmake_minimum_required(VERSION 3.6)

get_filename_component(PATH_TO_ROOT "../../../.." ABSOLUTE)

project("libHttpClient.Android")

include("../SetBinaryOutputDir.cmake")
set_binary_output_dir("${PATH_TO_ROOT}")

set(CMAKE_STATIC_LIBRARY_PREFIX "")

###########################################
### Set up paths for source and include ###
###########################################

include("../../SetCommonHCSourceFiles.cmake")
set_common_hc_source_files(COMMON_SOURCE_FILES COMMON_INCLUDE_DIRS "${PATH_TO_ROOT}/")

set(COMMON_ANDROID_SOURCE_FILES
    "${PATH_TO_ROOT}/Source/Common/Android/utils_android.cpp"
    "${PATH_TO_ROOT}/Source/Common/Android/utils_android.h"
    )

set(TASK_ANDROID_SOURCE_FILES
    "${PATH_TO_ROOT}/Include/httpClient/async_jvm.h"
    "${PATH_TO_ROOT}/Source/Task/ThreadPool_stl.cpp"
    "${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp"
    )

set(ANDROID_HTTP_SOURCE_FILES
    "${PATH_TO_ROOT}/Source/HTTP/Android/http_android.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.h"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_platform_context.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_platform_context.h"
    )

set(ANDROID_LOGGER_SOURCE_FILES
    "${PATH_TO_ROOT}/Source/Logger/Android/android_logger.cpp"
    )

set(ANDROID_WEBSOCKET_SOURCE_FILES
    "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.cpp"
    "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/x509_cert_utilities.hpp"
    )

set(ANDROID_INCLUDE_DIRS
    "${PATH_TO_ROOT}/External/generatedHeaders/android"
    )

#########################
### Set up static lib ###
#########################

add_library(
    "${PROJECT_NAME}"
    STATIC
    "${COMMON_SOURCE_FILES}"
    "${COMMON_ANDROID_SOURCE_FILES}"
    "${TASK_ANDROID_SOURCE_FILES}"
    "${ANDROID_HTTP_SOURCE_FILES}"
    "${ANDROID_LOGGER_SOURCE_FILES}"
    "${ANDROID_WEBSOCKET_SOURCE_FILES}"
)

target_include_directories(
    "${PROJECT_NAME}"
    PRIVATE
    "${COMMON_INCLUDE_DIRS}"
    "${ANDROID_INCLUDE_DIRS}"
)

set(FLAGS
    "-Wall"
    "-fexceptions"
    "-frtti"
    "-std=c++14"
    "-Wno-unknown-pragmas"
    "-Wno-pragma-once-outside-header"
    "-DHC_PLATFORM_MSBUILD_GUESS=HC_PLATFORM_ANDROID"
    )

set(FLAGS_DEBUG
    "-O0"
    "-DHC_TRACE_BUILD_LEVEL=5"
    )

set(FLAGS_RELEASE
    "-Os"
    "-DHC_TRACE_BUILD_LEVEL=3"
    )

include("../TargetSetFlags.cmake")
target_set_flags(
    "${PROJECT_NAME}"
    "${FLAGS}"
    "${FLAGS_DEBUG}"
    "${FLAGS_RELEASE}"
)

####################################
### Gradle static lib workaround ###
####################################

include("../WorkaroundToBuildStaticLibsInGradle.cmake")
workaround_to_build_static_libs_in_gradle("${PROJECT_NAME}")
