cmake_minimum_required(VERSION 3.14)

get_filename_component(PATH_TO_ROOT "../.." ABSOLUTE)

project("libHttpClient")

if(NOT WIN32)
    message(FATAL_ERROR "Build/libHttpClient.Win32.CMake is the Windows (HC_PLATFORM_WIN32) build. Use Build/libHttpClient.Linux, Build/libHttpClient.Android or Build/libHttpClient.iOS.CMake on other platforms.")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

#####################
### Build options ###
#####################

option(HC_NOWEBSOCKETS "Build without WebSocket support (defines HC_NOWEBSOCKETS)" OFF)
option(HC_NOZLIB "Build without zlib-backed request compression (defines HC_NOZLIB)" OFF)
option(HC_NOASYNC "Build without the XAsync/XTaskQueue implementation; the host must supply it" OFF)
option(HC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(HC_ENABLE_WEBSOCKET_COMPRESSION "Build the hybrid WinHTTP/websocketpp WebSocket provider (permessage-deflate)" OFF)

set(HC_ZLIB_SOURCE_DIR "${PATH_TO_ROOT}/External/zlib" CACHE PATH "zlib source tree")
set(HC_WEBSOCKETPP_INCLUDE_DIR "${PATH_TO_ROOT}/External/websocketpp" CACHE PATH "websocketpp headers")
set(HC_ASIO_INCLUDE_DIR "${PATH_TO_ROOT}/External/asio/asio/include" CACHE PATH "standalone asio headers")
set(HC_WINTLS_INCLUDE_DIR "${PATH_TO_ROOT}/External/boost-wintls/include" CACHE PATH "boost-wintls headers")

# WebSockets need RTTI (dynamic_pointer_cast on hc_websocket_impl).
if(NOT HC_NOWEBSOCKETS AND MSVC)
    set(_hc_all_cxx_flags "${CMAKE_CXX_FLAGS}")
    foreach(_hc_config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
        string(TOUPPER "${_hc_config}" _hc_config)
        string(APPEND _hc_all_cxx_flags " ${CMAKE_CXX_FLAGS_${_hc_config}}")
    endforeach()
    string(FIND "${_hc_all_cxx_flags}" "/GR-" _hc_rtti_disabled)
    if(NOT _hc_rtti_disabled EQUAL -1)
        message(FATAL_ERROR
            "libHttpClient: WebSocket support requires RTTI, but /GR- was found in the C++ flags.\n"
            "Either drop /GR-, or build without WebSockets by passing -DHC_NOWEBSOCKETS=ON.")
    endif()
endif()

# A DLL must export XAsync*/XTaskQueue*, so the implementation can only be omitted from a static build.
if(HC_NOASYNC AND BUILD_SHARED_LIBS)
    message(FATAL_ERROR
        "libHttpClient: HC_NOASYNC requires a static build. A shared library must export the "
        "XAsync/XTaskQueue entry points, so it cannot omit their implementation.\n"
        "Use a static triplet, or keep the 'async' feature enabled.")
endif()

# buildver.h is stamped by official builds; packagers pass the release tag via HC_PACKAGE_VERSION.
if(NOT HC_PACKAGE_VERSION)
    file(READ "${PATH_TO_ROOT}/Source/Common/buildver.h" HC_BUILDVER_H)
    string(REGEX MATCH "#define[ \t]+LIBHTTPCLIENT_VERSION[ \t]+\"([0-9.]+)\"" _hc_unused "${HC_BUILDVER_H}")
    set(HC_PACKAGE_VERSION "${CMAKE_MATCH_1}")
endif()
if(NOT HC_PACKAGE_VERSION)
    set(HC_PACKAGE_VERSION "0.0.0")
endif()
message(STATUS "libHttpClient package version: ${HC_PACKAGE_VERSION}")

####################
### Source files ###
####################

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

set(HC_SOURCE_FILES
    ${PUBLIC_SOURCE_FILES}
    ${HC_COMMON_SOURCE_FILES}
    ${GLOBAL_SOURCE_FILES}
    ${PLATFORM_SOURCE_FILES}
    ${MOCK_SOURCE_FILES}
    ${HTTP_SOURCE_FILES}
    ${LOGGER_SOURCE_FILES}
    "${PATH_TO_ROOT}/Source/Common/Types.h"
    "${PATH_TO_ROOT}/Source/Common/Win/utils_win.cpp"
    "${PATH_TO_ROOT}/Source/Common/Win/utils_win.h"
    "${PATH_TO_ROOT}/Source/Platform/Windows/PlatformTrace_Windows.cpp"
    "${PATH_TO_ROOT}/Source/Platform/Win32/PlatformComponents_Win32.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/WinHttp/winhttp_connection.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/WinHttp/winhttp_connection.h"
    "${PATH_TO_ROOT}/Source/HTTP/WinHttp/winhttp_provider.cpp"
    "${PATH_TO_ROOT}/Source/HTTP/WinHttp/winhttp_provider.h"
    "${PATH_TO_ROOT}/Source/HTTP/WinHttp/winhttp_proxy_helper.cpp"
    )

if(NOT HC_NOWEBSOCKETS)
    list(APPEND HC_SOURCE_FILES
        ${WEBSOCKET_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/WebSocket/websocket_options.h"
        )
endif()

if(HC_ENABLE_WEBSOCKET_COMPRESSION)
    if(HC_NOWEBSOCKETS)
        message(FATAL_ERROR "HC_ENABLE_WEBSOCKET_COMPRESSION requires WebSocket support; do not combine it with HC_NOWEBSOCKETS.")
    endif()
    if(HC_NOZLIB)
        message(FATAL_ERROR "HC_ENABLE_WEBSOCKET_COMPRESSION requires zlib; do not combine it with HC_NOZLIB.")
    endif()
    foreach(_hc_dep HC_WEBSOCKETPP_INCLUDE_DIR HC_ASIO_INCLUDE_DIR HC_WINTLS_INCLUDE_DIR)
        if(NOT EXISTS "${${_hc_dep}}")
            message(FATAL_ERROR
                "HC_ENABLE_WEBSOCKET_COMPRESSION: ${_hc_dep} does not exist (${${_hc_dep}}).\n"
                "Run 'git submodule update --init External/websocketpp External/asio External/boost-wintls', "
                "or set the HC_*_INCLUDE_DIR variables to your own copies.")
        endif()
    endforeach()

    set(HC_WSPP_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/HTTP/WinHttp/winhttp_websocket_hybrid.cpp"
        "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.cpp"
        )
    list(APPEND HC_SOURCE_FILES ${HC_WSPP_SOURCE_FILES})
endif()

# Reuses the OUT_TASK_SOURCE_FILES seam from GetCommonHCSourceFiles.cmake.
if(NOT HC_NOASYNC)
    list(APPEND HC_SOURCE_FILES
        ${TASK_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/Task/ThreadPool_win32.cpp"
        "${PATH_TO_ROOT}/Source/Task/WaitTimer_win32.cpp"
        "${PATH_TO_ROOT}/Source/Task/XTaskQueueDiagnostics.h"
        )
endif()

if(BUILD_SHARED_LIBS)
    if(HC_NOWEBSOCKETS)
        list(APPEND HC_SOURCE_FILES "${PATH_TO_ROOT}/Build/libHttpClient.Win32/libHttpClient.Win32.NoWebSockets.def")
    else()
        list(APPEND HC_SOURCE_FILES "${PATH_TO_ROOT}/Build/libHttpClient.Win32/libHttpClient.Win32.def")
    endif()
endif()

##############
### Target ###
##############

add_library(libHttpClient ${HC_SOURCE_FILES})
add_library(libHttpClient::libHttpClient ALIAS libHttpClient)

set_target_properties(libHttpClient PROPERTIES
    EXPORT_NAME libHttpClient
    OUTPUT_NAME libHttpClient
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    )

target_include_directories(libHttpClient
    PRIVATE
        "${PATH_TO_ROOT}/Source"
        "${PATH_TO_ROOT}/Source/Common"
        "${PATH_TO_ROOT}/Source/HTTP"
        "${PATH_TO_ROOT}/Source/Logger"
    PUBLIC
        $<BUILD_INTERFACE:${PATH_TO_ROOT}/Include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

target_compile_definitions(libHttpClient PRIVATE
    _WIN32_WINNT=0x601
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    )

if(HC_NOWEBSOCKETS)
    target_compile_definitions(libHttpClient PUBLIC HC_NOWEBSOCKETS)
    target_compile_definitions(libHttpClient PRIVATE HC_WINHTTP_WEBSOCKETS=0)
endif()
if(HC_NOZLIB)
    target_compile_definitions(libHttpClient PRIVATE HC_NOZLIB=1)
endif()

if(MSVC)
    # Hardening at parity with libHttpClient.Win32.props and libHttpClient.Win32.vcxproj:
    # /sdl everywhere, /Qspectre in Release, and EH continuation metadata on x64 Release.
    target_compile_options(libHttpClient PRIVATE /W4 /bigobj /Zc:strictStrings /sdl /guard:cf)
    target_compile_options(libHttpClient PRIVATE $<$<CONFIG:Release>:/Qspectre>)
    target_link_options(libHttpClient PRIVATE /guard:cf /DYNAMICBASE)
    target_link_options(libHttpClient PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)

    if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
        target_compile_options(libHttpClient PRIVATE $<$<CONFIG:Release>:/guard:ehcont>)
        target_link_options(libHttpClient PRIVATE $<$<CONFIG:Release>:/guard:ehcont>)
    endif()

    if(HC_WARNINGS_AS_ERRORS)
        target_compile_options(libHttpClient PRIVATE /WX)
    endif()
endif()

target_link_libraries(libHttpClient PUBLIC winhttp crypt32)

if(HC_ENABLE_WEBSOCKET_COMPRESSION)
    target_compile_definitions(libHttpClient PRIVATE HC_ENABLE_WEBSOCKET_COMPRESSION=1 _WEBSOCKETPP_CPP11_RANDOM_DEVICE_)
    target_include_directories(libHttpClient PRIVATE
        "${HC_WEBSOCKETPP_INCLUDE_DIR}"
        "${HC_ASIO_INCLUDE_DIR}"
        "${HC_WINTLS_INCLUDE_DIR}"
        )
    set_source_files_properties(${HC_WSPP_SOURCE_FILES} PROPERTIES
        COMPILE_DEFINITIONS "_CRT_NONSTDC_NO_WARNINGS;_WEBSOCKETPP_CPP11_STL_;ASIO_DISABLE_SERIAL_PORT")
endif()

if(NOT HC_NOZLIB)
    # Same file set the MSBuild projects compile (Build/libHttpClient.Zlib.vcxitems).
    if(NOT EXISTS "${HC_ZLIB_SOURCE_DIR}/zlib.h")
        message(FATAL_ERROR
            "No zlib sources at ${HC_ZLIB_SOURCE_DIR}. Run:\n"
            "  git submodule update --init External/zlib\n"
            "or set -DHC_ZLIB_SOURCE_DIR to a zlib source tree, "
            "or -DHC_NOZLIB=ON to build without compression.")
    endif()

    set(HC_ZLIB_SOURCE_FILES
        "${HC_ZLIB_SOURCE_DIR}/adler32.c"
        "${HC_ZLIB_SOURCE_DIR}/compress.c"
        "${HC_ZLIB_SOURCE_DIR}/crc32.c"
        "${HC_ZLIB_SOURCE_DIR}/deflate.c"
        "${HC_ZLIB_SOURCE_DIR}/gzclose.c"
        "${HC_ZLIB_SOURCE_DIR}/gzlib.c"
        "${HC_ZLIB_SOURCE_DIR}/gzread.c"
        "${HC_ZLIB_SOURCE_DIR}/gzwrite.c"
        "${HC_ZLIB_SOURCE_DIR}/infback.c"
        "${HC_ZLIB_SOURCE_DIR}/inffast.c"
        "${HC_ZLIB_SOURCE_DIR}/inflate.c"
        "${HC_ZLIB_SOURCE_DIR}/inftrees.c"
        "${HC_ZLIB_SOURCE_DIR}/trees.c"
        "${HC_ZLIB_SOURCE_DIR}/uncompr.c"
        "${HC_ZLIB_SOURCE_DIR}/zutil.c"
        "${HC_ZLIB_SOURCE_DIR}/contrib/minizip/ioapi.c"
        "${HC_ZLIB_SOURCE_DIR}/contrib/minizip/iowin32.c"
        "${HC_ZLIB_SOURCE_DIR}/contrib/minizip/unzip.c"
        )

    target_sources(libHttpClient PRIVATE ${HC_ZLIB_SOURCE_FILES})
    target_include_directories(libHttpClient PRIVATE
        "${HC_ZLIB_SOURCE_DIR}"
        "${HC_ZLIB_SOURCE_DIR}/contrib/minizip"
        )

    # Same suppressions the MSBuild projects apply to zlib.
    if(MSVC)
        set_source_files_properties(${HC_ZLIB_SOURCE_FILES} PROPERTIES
            COMPILE_OPTIONS "/wd4131;/wd4100;/wd4189;/wd4456;/wd4127")
    endif()
endif()

###############
### Install ###
###############

install(TARGETS libHttpClient
    EXPORT libHttpClientTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )

# Install only what PUBLIC_SOURCE_FILES declares; a glob would also ship the Android-only headers.
set(HC_PUBLIC_HEADERS_ROOT "")
set(HC_PUBLIC_HEADERS_HTTPCLIENT "")
foreach(_hc_header ${PUBLIC_SOURCE_FILES})
    get_filename_component(_hc_header_dir "${_hc_header}" DIRECTORY)
    get_filename_component(_hc_header_dir "${_hc_header_dir}" NAME)
    if(_hc_header_dir STREQUAL "httpClient")
        list(APPEND HC_PUBLIC_HEADERS_HTTPCLIENT "${_hc_header}")
    else()
        list(APPEND HC_PUBLIC_HEADERS_ROOT "${_hc_header}")
    endif()
endforeach()

install(FILES ${HC_PUBLIC_HEADERS_ROOT}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

install(FILES ${HC_PUBLIC_HEADERS_HTTPCLIENT}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/httpClient"
    )

######################
### Package config ###
######################

set(HC_INSTALL_CMAKEDIR "${CMAKE_INSTALL_DATAROOTDIR}/libHttpClient")

install(EXPORT libHttpClientTargets
    FILE libHttpClientTargets.cmake
    NAMESPACE libHttpClient::
    DESTINATION "${HC_INSTALL_CMAKEDIR}"
    )

# Consumer-facing config variables are the positive form of the HC_NO* switches, normalized to
# ON/OFF so the generated config never emits an empty value.
foreach(_hc_feature WEBSOCKETS ZLIB ASYNC)
    if(HC_NO${_hc_feature})
        set(HC_CONFIG_${_hc_feature} OFF)
    else()
        set(HC_CONFIG_${_hc_feature} ON)
    endif()
endforeach()

set(HC_CONFIG_WEBSOCKET_COMPRESSION OFF)
if(HC_ENABLE_WEBSOCKET_COMPRESSION)
    set(HC_CONFIG_WEBSOCKET_COMPRESSION ON)
endif()

set(HC_CONFIG_SHARED OFF)
if(BUILD_SHARED_LIBS)
    set(HC_CONFIG_SHARED ON)
endif()

configure_package_config_file(
    "../libHttpClient.CMake/libHttpClientConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/libHttpClientConfig.cmake"
    INSTALL_DESTINATION "${HC_INSTALL_CMAKEDIR}"
    )

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/libHttpClientConfigVersion.cmake"
    VERSION "${HC_PACKAGE_VERSION}"
    COMPATIBILITY SameMajorVersion
    )

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/libHttpClientConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/libHttpClientConfigVersion.cmake"
    DESTINATION "${HC_INSTALL_CMAKEDIR}"
    )
