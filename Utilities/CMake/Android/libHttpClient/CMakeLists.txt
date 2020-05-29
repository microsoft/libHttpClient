cmake_minimum_required(VERSION 3.6)

get_filename_component(PATH_TO_ROOT "../../../.." ABSOLUTE)

project("libHttpClient.Android")

include("../GetLibHCBinaryOutputDir.cmake")
get_libhc_binary_output_dir("${PATH_TO_ROOT}" LIBHC_BINARY_DIR)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${LIBHC_BINARY_DIR}")

set(CMAKE_STATIC_LIBRARY_PREFIX "")

###########################################
### Set up paths for source and include ###
###########################################

include("../../SetCommonHCSourceFiles.cmake")
set_common_hc_source_files(
    PUBLIC_SOURCE_FILES
    COMMON_SOURCE_FILES
    GLOBAL_SOURCE_FILES
    WEBSOCKET_SOURCE_FILES
    TASK_SOURCE_FILES
    MOCK_SOURCE_FILES
    HTTP_SOURCE_FILES
    LOGGER_SOURCE_FILES
    "${PATH_TO_ROOT}"
    )

include("../../SetAndroidHCSourceFiles.cmake")
set_android_hc_source_files(
    ANDROID_COMMON_SOURCE_FILES
    ANDROID_WEBSOCKET_SOURCE_FILES
    ANDROID_TASK_SOURCE_FILES
    ANDROID_HTTP_SOURCE_FILES
    ANDROID_LOGGER_SOURCE_FILES
    "${PATH_TO_ROOT}"
    )

set(COMMON_SOURCE_FILES
    "${PUBLIC_SOURCE_FILES}"
    "${COMMON_SOURCE_FILES}"
    "${GLOBAL_SOURCE_FILES}"
    "${WEBSOCKET_SOURCE_FILES}"
    "${TASK_SOURCE_FILES}"
    "${MOCK_SOURCE_FILES}"
    "${HTTP_SOURCE_FILES}"
    "${LOGGER_SOURCE_FILES}"
    )

set(ANDROID_SOURCE_FILES
    "${ANDROID_COMMON_SOURCE_FILES}"
    "${ANDROID_WEBSOCKET_SOURCE_FILES}"
    "${ANDROID_TASK_SOURCE_FILES}"
    "${ANDROID_HTTP_SOURCE_FILES}"
    "${ANDROID_LOGGER_SOURCE_FILES}"
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
    "${PATH_TO_ROOT}/External/generatedHeaders/android"
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
