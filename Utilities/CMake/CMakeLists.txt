cmake_minimum_required (VERSION 3.6)

if( SHIP )
    set (PROJECT_NAME libHttpClient.Ship)
elseif( UNITTEST )
    if( TAEF )
        set (PROJECT_NAME libHttpClient.UnitTest.TAEF)
    elseif( TE )
        set (PROJECT_NAME libHttpClient.UnitTest.TE)
    endif()
else()
    if( XDK )
        if( WINRT )
            set (PROJECT_NAME libHttpClient.XDK.WinRT)
        else()
            set (PROJECT_NAME libHttpClient.XDK.C)
        endif()
    elseif( BUILDWIN32 )
        set (PROJECT_NAME libHttpClient.Win32.C)
    elseif( GDK )
        set (PROJECT_NAME libHttpClient.GDK.C)
    else()
        if( WINRT )
            set (PROJECT_NAME libHttpClient.UWP.WinRT)
        else()
            set (PROJECT_NAME libHttpClient.UWP.C)
        endif()
    endif()
endif()


project (${PROJECT_NAME})

if(MSVC_VERSION GREATER 1919)
  set(COMPILER_VERSION "16")
elseif(MSVC_VERSION GREATER 1909)
  set(COMPILER_VERSION "15")
elseif(MSVC_VERSION GREATER 1899)
  set(COMPILER_VERSION "14")
elseif(MSVC_VERSION GREATER 1700)
  set(COMPILER_VERSION "12")
elseif(MSVC_VERSION GREATER 1600)
  set(COMPILER_VERSION "11")
endif()

if(CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CONFIGURATION_TYPES Debug)
  set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING "Reset the configurations to what we need" FORCE)
endif()

set(CMAKE_USE_RELATIVE_PATHS TRUE)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "WindowsStore")
  set(PLATFORM STORE)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_compile_options(/Zm300 /bigobj)
if (WINDOWS_STORE OR WINDOWS_PHONE)
    add_compile_options(/ZW)
endif()

add_definitions(-D_NO_ASYNCRTIMP -D_NO_PPLXIMP -D_NO_XSAPIIMP -DXSAPI_BUILD)

set_source_files_properties(../../../Source/Common/pch.cpp PROPERTIES COMPILE_FLAGS "/Ycpch.h")

if (NOT ${CMAKE_GENERATOR} MATCHES "Visual Studio .*")
    set_property(SOURCE ../../../Source/Common/pch.cpp APPEND PROPERTY OBJECT_OUTPUTS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")
    set_property(SOURCE ${SOURCES} APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Source
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Source/Common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Source/HTTP
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Source/Logger
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../Include/httpClient
    )

set(CMAKE_SUPPRESS_REGENERATION true)

set(Task_Windows_Source_Files
    ../../../Source/Task/ThreadPool_win32.cpp
    ../../../Source/Task/WaitTimer_win32.cpp
    )

set(WinRT_WebSocket_Source_Files
    ../../../Source/WebSocket/WinRT/winrt_websocket.cpp
    ../../../Source/WebSocket/WinRT/winrt_websocket.h
    )

set(Win32_WebSocket_Source_Files
    ../../../Source/WebSocket/Websocketpp/websocketpp_websocket.cpp
    ../../../Source/WebSocket/Websocketpp/websocketpp_websocket.h
    ../../../Source/WebSocket/Websocketpp/x509_cert_utilities.hpp
    )

set(GDK_WebSocket_Source_Files
    ../../../Source/HTTP/WinHttp/winhttp_provider.cpp
    ../../../Source/HTTP/WinHttp/winhttp_provider.h
    ../../../Source/HTTP/WinHttp/winhttp_connection.cpp
    ../../../Source/HTTP/WinHttp/winhttp_connection.h
    )

set(Common_Windows_Source_Files
    ../../../Source/Common/Win/utils_win.cpp
    ../../../Source/Common/Win/utils_win.h
    )

set(WinHttp_HTTP_Source_Files
    ../../../Source/HTTP/WinHttp/winhttp_provider.cpp
    ../../../Source/HTTP/WinHttp/winhttp_provider.h
    ../../../Source/HTTP/WinHttp/winhttp_connection.cpp
    ../../../Source/HTTP/WinHttp/winhttp_connection.h
    )

set(XMLHttp_HTTP_Source_Files
    ../../../Source/HTTP/XMLHttp/http_request_callback.cpp
    ../../../Source/HTTP/XMLHttp/http_request_callback.h
    ../../../Source/HTTP/XMLHttp/http_response_stream.cpp
    ../../../Source/HTTP/XMLHttp/http_response_stream.h
    ../../../Source/HTTP/XMLHttp/http_request_stream.cpp
    ../../../Source/HTTP/XMLHttp/http_request_stream.h
    ../../../Source/HTTP/XMLHttp/xmlhttp_http_task.cpp
    ../../../Source/HTTP/XMLHttp/xmlhttp_http_task.h
    )

set(Curl_Source_Files
    ../../../Source/HTTP/Curl/CurlEasyRequest.cpp
    ../../../Source/HTTP/Curl/CurlEasyRequest.h
    ../../../Source/HTTP/Curl/CurlMulti.cpp
    ../../../Source/HTTP/Curl/CurlMulti.h
    ../../../Source/HTTP/Curl/CurlProvider.cpp
    ../../../Source/HTTP/Curl/CurlProvider.h
    )

set(Windows_Logger_Source_Files
   ../../../Source/Logger/Win/win_logger.cpp
   )

set(UnitTests_Source_Files_Support
    ../../../Tests/UnitTests/Support/DefineTestMacros.h
    ../../../Tests/UnitTests/Support/UnitTestIncludes.h
    )

set(Taef_UnitTests_Source_Files
    ../../../Tests/UnitTests/Support/TAEF/UnitTestBase.h
    ../../../Tests/UnitTests/Support/TAEF/UnitTestBase_winrt.h
    ../../../Tests/UnitTests/Support/TAEF/UnitTestIncludes_TAEF.h
    ../../../Tests/UnitTests/Support/TAEF/UnitTestBase.cpp
    ../../../Tests/UnitTests/Support/TAEF/UnitTestBase_winrt.cpp
    )

set(TE_UnitTests_Source_Files
    ../../../Tests/UnitTests/Support/TE/unittesthelpers.cpp
    ../../../Tests/UnitTests/Support/TE/unittesthelpers.h
    ../../../Tests/UnitTests/Support/TE/unittestincludes_te.h
    )

set(UnitTests_Source_Files_Tests
    ../../../Tests/UnitTests/Tests/AsyncBlockTests.cpp
    ../../../Tests/UnitTests/Tests/CallbackThunk.h
    ../../../Tests/UnitTests/Tests/GlobalTests.cpp
    ../../../Tests/UnitTests/Tests/HttpTests.cpp
    ../../../Tests/UnitTests/Tests/LocklessQueueTests.cpp
    ../../../Tests/UnitTests/Tests/MockTests.cpp
    ../../../Tests/UnitTests/Tests/TaskQueueTests.cpp
    ../../../Tests/UnitTests/Tests/WebsocketTests.cpp
    )

include(GetCommonHCSourceFiles.cmake)
get_common_hc_source_files(
    Public_Source_Files
    Common_Source_Files
    Global_Source_Files
    WebSocket_Source_Files
    Task_Source_Files
    Mock_Source_Files
    HTTP_Source_Files
    Logger_Source_Files
    "../../.."
    )

message(STATUS "Common source group")
source_group("Header Files" FILES ${Public_Source_Files})
source_group("C++ Source\\Common" FILES ${Common_Source_Files})
source_group("C++ Source\\Global" FILES ${Global_Source_Files})
source_group("C++ Source\\WebSocket" FILES ${WebSocket_Source_Files})
source_group("C++ Source\\Mock" FILES ${Mock_Source_Files})
source_group("C++ Source\\HTTP" FILES ${HTTP_Source_Files})
source_group("C++ Source\\Logger" FILES ${Logger_Source_Files})

set( SOURCE_FILES
    ${Public_Source_Files}
    ${Common_Source_Files}
    ${Global_Source_Files}
    ${WebSocket_Source_Files}
    ${Mock_Source_Files}
    ${HTTP_Source_Files}
    ${Logger_Source_Files}
    )

if( XDK )
    message(STATUS "XDK source group")
endif()

if( UNITTEST )
    message(STATUS "Unittest source group")
    source_group("C++ Source\\WebSocket\\Unittest" FILES ${Unittest_WebSocket_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})
    source_group("C++ Source\\Logger\\Win" FILES ${Windows_Logger_Source_Files})
    source_group("C++ Source\\Task\\Win" FILES ${Task_Windows_Source_Files})
    source_group("C++ Source\\Task" FILES ${Task_Source_Files})
    list(APPEND
        SOURCE_FILES
        ${Task_Windows_Source_Files}
        ${Common_Windows_Source_Files}
        ${Windows_Logger_Source_Files}
        ${Unittest_WebSocket_Source_Files}
        ${Task_Source_Files}
        )
endif()

if( UWP )
    message(STATUS "UWP source group")
    source_group("C++ Source\\HTTP\\XMLHttp" FILES ${XMLHttp_HTTP_Source_Files})
    source_group("C++ Source\\WebSocket\\WinRT" FILES ${WinRT_WebSocket_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})
    source_group("C++ Source\\Logger\\Win" FILES ${Windows_Logger_Source_Files})
    source_group("C++ Source\\Task\\Win" FILES ${Task_Windows_Source_Files})
    source_group("C++ Source\\Task" FILES ${Task_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${Task_Windows_Source_Files}
        ${XMLHttp_HTTP_Source_Files}
        ${Common_Windows_Source_Files}
        ${Windows_Logger_Source_Files}
        ${WinRT_WebSocket_Source_Files}
        ${Task_Source_Files}
        )
endif()

if( XDK )
    source_group("C++ Source\\HTTP\\XMLHttp" FILES ${XMLHttp_HTTP_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})
    source_group("C++ Source\\WebSocket\\WinRT" FILES ${WinRT_WebSocket_Source_Files})
    source_group("C++ Source\\Logger\\Win" FILES ${Windows_Logger_Source_Files})
    source_group("C++ Source\\Task\\Win" FILES ${Task_Windows_Source_Files})
    source_group("C++ Source\\Task" FILES ${Task_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${Task_Windows_Source_Files}
        ${XMLHttp_HTTP_Source_Files}
        ${Common_Windows_Source_Files}
        ${Windows_Logger_Source_Files}
        ${WinRT_WebSocket_Source_Files}
        ${Task_Source_Files}
        )
endif()

if( BUILDWIN32 )
    source_group("C++ Source\\HTTP\\WinHttp" FILES ${WinHttp_HTTP_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})
    source_group("C++ Source\\WebSocket\\Win" FILES ${Win32_WebSocket_Source_Files})
    source_group("C++ Source\\Logger\\Win" FILES ${Windows_Logger_Source_Files})
    source_group("C++ Source\\Task\\Win" FILES ${Task_Windows_Source_Files})
    source_group("C++ Source\\Task" FILES ${Task_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${Task_Windows_Source_Files}
        ${WinHttp_HTTP_Source_Files}
        ${Common_Windows_Source_Files}
        ${Windows_Logger_Source_Files}
        ${Win32_WebSocket_Source_Files}
        ${Task_Source_Files}
        )
endif()

if( GDK )
    message(STATUS "GDK source group")
    source_group("C++ Source\\HTTP\\Curl" FILES ${Curl_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})
    source_group("C++ Source\\WebSocket\\Win" FILES ${GDK_WebSocket_Source_Files})
    source_group("C++ Source\\Logger\\Win" FILES ${Windows_Logger_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${Curl_Source_Files}
        ${Common_Windows_Source_Files}
        ${Windows_Logger_Source_Files}
        ${GDK_WebSocket_Source_Files}
        )
endif()

if( TAEF )
    message(STATUS "TAEF source group")
    source_group("C++ Source\\UnitTests\\Support" FILES ${UnitTests_Source_Files_Support})
    source_group("C++ Source\\UnitTests\\Tests" FILES ${UnitTests_Source_Files_Tests})
    source_group("C++ Source\\UnitTests\\Support" FILES ${Taef_UnitTests_Source_Files})
    source_group("C++ Source\\Task" FILES ${Task_Source_Files})
    list(APPEND
        SOURCE_FILES
        ${UnitTests_Source_Files_Support}
        ${UnitTests_Source_Files_Tests}
        ${Taef_UnitTests_Source_Files}
        ${Task_Source_Files}
        )
endif()

if( TE )
    message(STATUS "TE source group")
    source_group("C++ Source\\UnitTests\\Support" FILES ${UnitTests_Source_Files_Support})
    source_group("C++ Source\\UnitTests\\Tests" FILES ${UnitTests_Source_Files_Tests})
    source_group("C++ Source\\UnitTests\\Support" FILES ${TE_UnitTests_Source_Files})
    source_group("C++ Source\\Task" FILES ${Task_Source_Files})
    list(APPEND
        SOURCE_FILES
        ${UnitTests_Source_Files_Support}
        ${UnitTests_Source_Files_Tests}
        ${TE_UnitTests_Source_Files}
        ${Task_Source_Files}
        )
endif()

list( SORT SOURCE_FILES )
add_library(${PROJECT_NAME} ${SOURCE_FILES})

set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS "/INCREMENTAL:NO")
set(CMAKE_STATIC_LINKER_FLAGS "/INCREMENTAL:NO")

message(STATUS "CMAKE_SYSTEM_VERSION='${CMAKE_SYSTEM_VERSION}'")
message(STATUS "CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'")
message(STATUS "SHORT_VERSION='${SHORT_VERSION}'")
