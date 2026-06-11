cmake_minimum_required(VERSION 3.14)

get_filename_component(PATH_TO_ROOT "../.." ABSOLUTE)

project("libHttpClient.iOS")

set(HC_WEBSOCKETS FALSE)

add_definitions(-D_NO_ASYNCRTIMP -D_NO_PPLXIMP -D_NO_XSAPIIMP -DXSAPI_BUILD -DHC_LINK_STATIC=1)

if(HC_WEBSOCKETS)
    message(STATUS "websockets usage is on")
else()
    message(STATUS "websockets usage is off")
    add_definitions(-DHC_NOWEBSOCKETS)
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
    "${MOCK_SOURCE_FILES}"
    "${HTTP_SOURCE_FILES}"
    "${LOGGER_SOURCE_FILES}"
    "${TASK_SOURCE_FILES}"
    )

set(IOS_SOURCE_FILES
    # Task
    "${PATH_TO_ROOT}/Source/Task/ThreadPool_stl.cpp"
    "${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp"
    "${PATH_TO_ROOT}/Source/Task/iOS/ios_WaitTimer.mm"
    "${PATH_TO_ROOT}/Source/Task/iOS/ios_WaitTimer_target.mm"
    "${PATH_TO_ROOT}/Source/Task/iOS/ios_WaitTimer_target.h"
    "${PATH_TO_ROOT}/Source/Task/iOS/ios_WaitTimerImpl.h"
    # HTTP
    "${PATH_TO_ROOT}/Source/HTTP/Apple/http_apple.mm"
    "${PATH_TO_ROOT}/Source/HTTP/Apple/http_apple.h"
    # Common
    "${PATH_TO_ROOT}/Source/Common/Apple/utils_apple.mm"
    "${PATH_TO_ROOT}/Source/Common/Apple/utils_apple.h"
    # Platform
    "${PATH_TO_ROOT}/Source/Platform/Apple/PlatformComponents_Apple.cpp"
    "${PATH_TO_ROOT}/Source/Platform/Apple/PlatformTrace_Apple.cpp"
    )

set(COMMON_INCLUDE_DIRS
    "${PATH_TO_ROOT}/"
    "${PATH_TO_ROOT}/Source/"
    "${PATH_TO_ROOT}/Source/Common"
    "${PATH_TO_ROOT}/Source/HTTP"
    "${PATH_TO_ROOT}/Source/Logger"
    "${PATH_TO_ROOT}/Source/Platform"
    "${PATH_TO_ROOT}/Source/Task"
    "${PATH_TO_ROOT}/Source/WebSocket"
    "${PATH_TO_ROOT}/Include"
    "${PATH_TO_ROOT}/Include/httpClient"
    )

#########################
### Set up static lib ###
#########################

set(ALL_SOURCE_FILES ${COMMON_SOURCE_FILES} ${IOS_SOURCE_FILES})
list(SORT ALL_SOURCE_FILES)

add_library(
    "${PROJECT_NAME}"
    ${ALL_SOURCE_FILES}
    )

target_include_directories(
    "${PROJECT_NAME}"
    PRIVATE
    "${COMMON_INCLUDE_DIRS}"
    )

set_target_properties(
    "${PROJECT_NAME}"
    PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    )

set(IOS_MM_SOURCE_FILES
    "${PATH_TO_ROOT}/Source/Task/iOS/ios_WaitTimer.mm"
    "${PATH_TO_ROOT}/Source/Task/iOS/ios_WaitTimer_target.mm"
    "${PATH_TO_ROOT}/Source/HTTP/Apple/http_apple.mm"
    "${PATH_TO_ROOT}/Source/Common/Apple/utils_apple.mm"
    )
set_source_files_properties(${IOS_MM_SOURCE_FILES} PROPERTIES COMPILE_FLAGS "-fobjc-arc")

message(STATUS "CMAKE_SYSTEM_VERSION='${CMAKE_SYSTEM_VERSION}'")
message(STATUS "CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'")
