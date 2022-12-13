cmake_minimum_required (VERSION 3.6)

option(LIBHTTPCLIENT_OBJECT_LIBRARY OFF)

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
    elseif(DEFINED LINUX)
        set (PROJECT_NAME libHttpClient.Linux.C)
    elseif( APPLE )
        set (PROJECT_NAME libHttpClient.Apple.C)
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

if(DEFINED MSVC_VERSION)
    add_compile_options(/Zm300 /bigobj)
endif()

if (WINDOWS_STORE OR WINDOWS_PHONE)
    add_compile_options(/ZW)
endif()

add_definitions(-D_NO_ASYNCRTIMP -D_NO_PPLXIMP -D_NO_XSAPIIMP -DXSAPI_BUILD)

if(DEFINED MSVC_VERSION)
    set_source_files_properties(../../../Source/Common/pch.cpp PROPERTIES COMPILE_FLAGS "/Ycpch.h")
endif()

if (NOT ${CMAKE_GENERATOR} MATCHES "Visual Studio .*")
    set_property(SOURCE ../../../Source/Common/pch.cpp APPEND PROPERTY OBJECT_OUTPUTS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")
endif()
    set_property(SOURCE ${SOURCES} APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")

if(DEFINED MSVC_VERSION)
    add_compile_options(/Zm250 /bigobj)
    if (WINDOWS_STORE OR WINDOWS_PHONE)
        add_compile_options(/ZW)
    endif()
endif()

set(CMAKE_SUPPRESS_REGENERATION true)

set(Task_Windows_Source_Files
    ../../Source/Task/ThreadPool_win32.cpp
    ../../Source/Task/WaitTimer_win32.cpp
    )

set(Task_STL_Source_Files
    ../../Source/Task/ThreadPool_stl.cpp
    ../../Source/Task/WaitTimer_stl.cpp
    )

set(Task_Apple_Source_Files
    ../../Source/Task/ThreadPool_stl.cpp
    ../../Source/Task/iOS/ios_WaitTimer.mm
    ../../Source/Task/iOS/ios_WaitTimer_target.h
    ../../Source/Task/iOS/ios_WaitTimer_target.mm
    ../../Source/Task/iOS/ios_WaitTimerImpl.h
    )

set(WinRT_WebSocket_Source_Files
    ../../Source/WebSocket/WinRT/winrt_websocket.cpp
    ../../Source/WebSocket/WinRT/winrt_websocket.h
    )

set(Websocketpp_Source_Files
        ../../Source/WebSocket/Websocketpp/websocketpp_websocket.cpp
        ../../Source/WebSocket/Websocketpp/websocketpp_websocket.h
        ../../Source/WebSocket/Websocketpp/x509_cert_utilities.hpp
        )

set(Win32_WebSocket_Source_Files
    ${Websocketpp_Source_Files}
    )

set(GDK_WebSocket_Source_Files
    ../../Source/HTTP/WinHttp/winhttp_provider.cpp
    ../../Source/HTTP/WinHttp/winhttp_provider.h
    ../../Source/HTTP/WinHttp/winhttp_connection.cpp
    ../../Source/HTTP/WinHttp/winhttp_connection.h
    )

set(Common_Windows_Source_Files
    ../../Source/Common/Win/utils_win.cpp
    ../../Source/Common/Win/utils_win.h
    )

set(WinHttp_HTTP_Source_Files
    ../../Source/HTTP/WinHttp/winhttp_provider.cpp
    ../../Source/HTTP/WinHttp/winhttp_provider.h
    ../../Source/HTTP/WinHttp/winhttp_connection.cpp
    ../../Source/HTTP/WinHttp/winhttp_connection.h
    )

set(XMLHttp_HTTP_Source_Files
    ../../Source/HTTP/XMLHttp/http_request_callback.cpp
    ../../Source/HTTP/XMLHttp/http_request_callback.h
    ../../Source/HTTP/XMLHttp/http_response_stream.cpp
    ../../Source/HTTP/XMLHttp/http_response_stream.h
    ../../Source/HTTP/XMLHttp/http_request_stream.cpp
    ../../Source/HTTP/XMLHttp/http_request_stream.h
    ../../Source/HTTP/XMLHttp/xmlhttp_http_task.cpp
    ../../Source/HTTP/XMLHttp/xmlhttp_http_task.h
    )

set(Apple_HTTP_Source_Files
        ../../Source/HTTP/Apple/http_apple.h
        ../../Source/HTTP/Apple/http_apple.mm
        ../../Source/HTTP/Apple/request_body_stream.h
        ../../Source/HTTP/Apple/request_body_stream.mm
        ../../Source/HTTP/Apple/session_delegate.h
        ../../Source/HTTP/Apple/session_delegate.mm
        )

set(Android_Source_Files
        ../../Include/httpClient/async_jvm.h
        ../../Source/Common/Android/utils_android.cpp
        ../../Source/Common/Android/utils_android.h
        ../../Source/HTTP/Android/http_android.cpp
        ../../Source/HTTP/Android/http_android.h
        ../../Source/HTTP/Android/android_http_request.cpp
        ../../Source/HTTP/Android/android_http_request.h
        ../../Source/HTTP/Android/android_platform_context.cpp
        ../../Source/HTTP/Android/android_platform_context.h
        ../../Source/Logger/Android/android_logger.cpp
        )


set(Curl_Source_Files
    ../../Source/HTTP/Curl/CurlEasyRequest.cpp
    ../../Source/HTTP/Curl/CurlEasyRequest.h
    ../../Source/HTTP/Curl/CurlMulti.cpp
    ../../Source/HTTP/Curl/CurlMulti.h
    ../../Source/HTTP/Curl/CurlProvider.cpp
    ../../Source/HTTP/Curl/CurlProvider.h
    )

set(Windows_Logger_Source_Files
   ../../Source/Logger/Win/win_logger.cpp
   )

set(Generic_Logger_Source_Files
   ../../Source/Logger/Generic/generic_logger.cpp
   )

set(UnitTests_Source_Files_Support
    ../../Tests/UnitTests/Support/DefineTestMacros.h
    ../../Tests/UnitTests/Support/UnitTestIncludes.h
    )

set(Taef_UnitTests_Source_Files
    ../../Tests/UnitTests/Support/TAEF/UnitTestBase.h
    ../../Tests/UnitTests/Support/TAEF/UnitTestBase_winrt.h
    ../../Tests/UnitTests/Support/TAEF/UnitTestIncludes_TAEF.h
    ../../Tests/UnitTests/Support/TAEF/UnitTestBase.cpp
    ../../Tests/UnitTests/Support/TAEF/UnitTestBase_winrt.cpp
    )

set(TE_UnitTests_Source_Files
    ../../Tests/UnitTests/Support/TE/unittesthelpers.cpp
    ../../Tests/UnitTests/Support/TE/unittesthelpers.h
    ../../Tests/UnitTests/Support/TE/unittestincludes_te.h
    )

set(UnitTests_Source_Files_Tests
    ../../Tests/UnitTests/Tests/AsyncBlockTests.cpp
    ../../Tests/UnitTests/Tests/CallbackThunk.h
    ../../Tests/UnitTests/Tests/GlobalTests.cpp
    ../../Tests/UnitTests/Tests/HttpTests.cpp
    ../../Tests/UnitTests/Tests/LocklessQueueTests.cpp
    ../../Tests/UnitTests/Tests/MockTests.cpp
    ../../Tests/UnitTests/Tests/TaskQueueTests.cpp
    ../../Tests/UnitTests/Tests/WebsocketTests.cpp
    )

include(GetCommonHCSourceFiles.cmake)
get_common_hc_source_files(
    Common_Source_Files
    Global_Source_Files
    WebSocket_Source_Files
    Task_Source_Files
    Mock_Source_Files
    HTTP_Source_Files
    Logger_Source_Files
    "../.."
    )

message(STATUS "Common source group")
source_group("C++ Source\\Common" FILES ${Common_Source_Files})
source_group("C++ Source\\Global" FILES ${Global_Source_Files})
source_group("C++ Source\\WebSocket" FILES ${WebSocket_Source_Files})
source_group("C++ Source\\Mock" FILES ${Mock_Source_Files})
source_group("C++ Source\\HTTP" FILES ${HTTP_Source_Files})
source_group("C++ Source\\Logger" FILES ${Logger_Source_Files})

set( SOURCE_FILES
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
    list(APPEND Additional_Libs ixmlhttprequest2)
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
    add_compile_definitions(UNICODE WIN32 _WIN32)
    find_package(websocketpp CONFIG REQUIRED)
    find_package(asio CONFIG REQUIRED)
    find_package(OpenSSL REQUIRED)
    list(APPEND Additional_Libs websocketpp::websocketpp asio::asio OpenSSL::SSL winhttp crypt32)
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
    list(APPEND Additional_Libs winhttp crypt32)
endif()

if (DEFINED LINUX)
    list(APPEND
            SOURCE_FILES
            ${Task_STL_Source_Files}
            ${Curl_Source_Files}
            ${Websocketpp_Source_Files}
            ${Task_Source_Files}
            ${Generic_Logger_Source_Files}
            )
    list(APPEND Additional_Libs CURL::libcurl)
endif()

if( APPLE )
    list(APPEND
        SOURCE_FILES
        ${Apple_HTTP_Source_Files}
        ${Task_Source_Files}
        ${Task_Apple_Source_Files}
        ${Generic_Logger_Source_Files}
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

add_compile_definitions(
        _VARIADIC_MAX=10
        ENABLE_INTSAFE_SIGNED_FUNCTIONS
)

list( SORT SOURCE_FILES )

if (${LIBHTTPCLIENT_OBJECT_LIBRARY})
    add_library(${PROJECT_NAME} OBJECT ${SOURCE_FILES})
else()
    add_library(${PROJECT_NAME} ${SOURCE_FILES})
endif()


set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 14)

target_include_directories(${PROJECT_NAME}
        PRIVATE
        ../..
        ../../Source
        ../../Source/Common
        ../../Source/HTTP
        ../../Source/Logger
        ../../Include/httpClient
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Include>
        $<INSTALL_INTERFACE:Include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../Include/httpClient>
        $<INSTALL_INTERFACE:Include/httpClient>
)

target_link_libraries(${PROJECT_NAME}
        PRIVATE ${Additional_Libs}
        )

if (DEFINED MSVC_VERSION)
    set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS "/INCREMENTAL:NO")
    set(CMAKE_STATIC_LINKER_FLAGS "/INCREMENTAL:NO")
endif()

if (APPLE)
    target_link_libraries(${PROJECT_NAME} "-framework Foundation")
    target_compile_options(${PROJECT_NAME} PRIVATE "-fobjc-arc")
endif()

message(STATUS "CMAKE_SYSTEM_VERSION='${CMAKE_SYSTEM_VERSION}'")
message(STATUS "CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'")
message(STATUS "SHORT_VERSION='${SHORT_VERSION}'")
