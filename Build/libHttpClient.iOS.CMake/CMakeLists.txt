cmake_minimum_required(VERSION 3.14)

get_filename_component(PATH_TO_ROOT "../.." ABSOLUTE)

project("libHttpClient.iOS")

# Restore third-party sources into External/ if missing or out of date (see Build/vcpkg).
include("${CMAKE_CURRENT_LIST_DIR}/../vcpkg/AutoRestore.cmake")

# Off by default. On builds the websocketpp WebSocket provider, which needs OpenSSL headers in
# HC_OPENSSL_INCLUDE_DIR. The app must also link OpenSSL's libssl and libcrypto.
option(HC_WEBSOCKETS "Build the websocketpp WebSocket provider" OFF)
if(NOT DEFINED HC_OPENSSL_INCLUDE_DIR)
    set(HC_OPENSSL_INCLUDE_DIR "" CACHE PATH "OpenSSL include directory, used when HC_WEBSOCKETS is on")
endif()

# Empty by default. When set, the Objective-C classes get this prefix (see objc_class_prefix_apple.h).
if(NOT DEFINED HC_OBJC_CLASS_PREFIX)
    set(HC_OBJC_CLASS_PREFIX "" CACHE STRING "Prefix for libHttpClient's Objective-C class names")
endif()

add_definitions(-D_NO_ASYNCRTIMP -D_NO_PPLXIMP -D_NO_XSAPIIMP -DXSAPI_BUILD -DHC_LINK_STATIC=1)

if(HC_WEBSOCKETS)
    message(STATUS "websockets usage is on")
    if(NOT HC_OPENSSL_INCLUDE_DIR)
        message(FATAL_ERROR "HC_WEBSOCKETS requires HC_OPENSSL_INCLUDE_DIR")
    endif()
else()
    message(STATUS "websockets usage is off")
    add_definitions(-DHC_NOWEBSOCKETS)
endif()

if(HC_OBJC_CLASS_PREFIX)
    if(NOT HC_OBJC_CLASS_PREFIX MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "HC_OBJC_CLASS_PREFIX must be a valid identifier prefix: '${HC_OBJC_CLASS_PREFIX}'")
    endif()
    message(STATUS "Objective-C class prefix: ${HC_OBJC_CLASS_PREFIX}")
    add_definitions(-DHC_OBJC_CLASS_PREFIX=${HC_OBJC_CLASS_PREFIX})
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
    # Only the STL wait timer, as in the Xcode project. iOS/ios_WaitTimer.mm defines the same
    # OS::WaitTimer, so building both gives duplicate symbols.
    "${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp"
    # HTTP
    "${PATH_TO_ROOT}/Source/HTTP/Apple/http_apple.mm"
    "${PATH_TO_ROOT}/Source/HTTP/Apple/http_apple.h"
    "${PATH_TO_ROOT}/Source/HTTP/Apple/request_body_stream.mm"
    "${PATH_TO_ROOT}/Source/HTTP/Apple/request_body_stream.h"
    "${PATH_TO_ROOT}/Source/HTTP/Apple/session_delegate.mm"
    "${PATH_TO_ROOT}/Source/HTTP/Apple/session_delegate.h"
    # Common
    "${PATH_TO_ROOT}/Source/Common/Apple/objc_class_prefix_apple.h"
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

if(HC_WEBSOCKETS)
    list(APPEND IOS_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.cpp"
        "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.h"
        )
    list(APPEND COMMON_INCLUDE_DIRS
        "${PATH_TO_ROOT}/External/asio/asio/include"
        "${PATH_TO_ROOT}/External/websocketpp"
        "${HC_OPENSSL_INCLUDE_DIR}"
        )
endif()

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
    "${PATH_TO_ROOT}/Source/HTTP/Apple/http_apple.mm"
    "${PATH_TO_ROOT}/Source/HTTP/Apple/request_body_stream.mm"
    "${PATH_TO_ROOT}/Source/HTTP/Apple/session_delegate.mm"
    "${PATH_TO_ROOT}/Source/Common/Apple/utils_apple.mm"
    )
set_source_files_properties(${IOS_MM_SOURCE_FILES} PROPERTIES COMPILE_FLAGS "-fobjc-arc")

message(STATUS "CMAKE_SYSTEM_VERSION='${CMAKE_SYSTEM_VERSION}'")
message(STATUS "CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'")
