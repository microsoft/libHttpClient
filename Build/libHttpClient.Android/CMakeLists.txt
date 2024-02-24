cmake_minimum_required(VERSION 3.6)

get_filename_component(PATH_TO_ROOT "../.." ABSOLUTE)

project("libHttpClient.Android")

option(BUILD_SHARED_LIBS "Build libHC Shared Library")

set(CMAKE_STATIC_LIBRARY_PREFIX "")
if(BUILD_SHARED_LIBS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(CMAKE_CXX_FLAGS_RELEASE "-gdwarf-5")
    set(CMAKE_C_FLAGS_RELEASE "-gdwarf-5")
    set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-z,now")
endif()

###########################################
### Set up paths for source and include ###
###########################################

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

set(ANDROID_SOURCE_FILES
    "${PATH_TO_ROOT}/Include/httpClient/async_jvm.h"
    "${PATH_TO_ROOT}/Source/HTTP/Android/http_android.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Android/http_android.h"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.h"
    "${PATH_TO_ROOT}/Source/HTTP/Android/AndroidHttpProvider.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/Android/AndroidHttpProvider.h"
    "${PATH_TO_ROOT}/Source/Platform/Android/PlatformComponents_Android.cpp"
    "${PATH_TO_ROOT}/Source/Platform/Android/PlatformComponents_Android.h"
    "${PATH_TO_ROOT}/Source/Platform/Android/PlatformTrace_Android.cpp"
    "${PATH_TO_ROOT}/Source/Task/ThreadPool_stl.cpp"
    "${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp"
    "${PATH_TO_ROOT}/Source/WebSocket/Android/AndroidWebSocketProvider.cpp"
    "${PATH_TO_ROOT}/Source/WebSocket/Android/AndroidWebSocketProvider.h"
    )

 set(COMMON_INCLUDE_DIRS
    "${PATH_TO_ROOT}/Source"
    "${PATH_TO_ROOT}/Source/Common"
    "${PATH_TO_ROOT}/Source/HTTP"
    "${PATH_TO_ROOT}/Source/Logger"
    "${PATH_TO_ROOT}/Source/Platform"
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
    "${COMMON_SOURCE_FILES}"
    "${ANDROID_SOURCE_FILES}"
)

target_include_directories(
    "${PROJECT_NAME}"
    PRIVATE
    "${COMMON_INCLUDE_DIRS}"
    "${ANDROID_INCLUDE_DIRS}"
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
