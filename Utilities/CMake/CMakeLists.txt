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
    else()
        if( WINRT )
            set (PROJECT_NAME libHttpClient.UWP.WinRT)
        else()
            set (PROJECT_NAME libHttpClient.UWP.C)
        endif()
    endif()
endif()

macro(print_variables)
    message(STATUS "print_variables ------------------------{")
    get_cmake_property(_variableNames VARIABLES)
    foreach (_variableName ${_variableNames})
        message(STATUS "${_variableName}=${${_variableName}}")
    endForEach()
    message(STATUS "print_variables ------------------------}")
endmacro()

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

set(REL_PATH_TO_PROJECT_DIR "../../../")

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
set_source_files_properties(${REL_PATH_TO_PROJECT_DIR}Source/Common/pch.cpp PROPERTIES COMPILE_FLAGS "/Ycpch.h")

if (NOT ${CMAKE_GENERATOR} MATCHES "Visual Studio .*")
  set_property(SOURCE ${REL_PATH_TO_PROJECT_DIR}Source/Common/pch.cpp APPEND PROPERTY OBJECT_OUTPUTS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")
  set_property(SOURCE ${SOURCES} APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")
endif()

set(CMAKE_SUPPRESS_REGENERATION true)

set(Task_Windows_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/ThreadPool_win32.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/WaitTimer_win32.cpp
    )

set(WinRT_WebSocket_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/WinRT/winrt_websocket.cpp
    )

set(Win32_WebSocket_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/Websocketpp/websocketpp_websocket.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/Websocketpp/x509_cert_utilities.hpp
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/WinHTTP/winhttp_websocket.cpp
    )

set(Common_Windows_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/Win/utils_win.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/Win/utils_win.h
    )

set(Unittest_HTTP_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/Unittest/http_unittest.cpp
    )

set(Unittest_WebSocket_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/Unittest/websocket_unittest.cpp
    )

set(WinHttp_HTTP_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/WinHttp/winhttp_http_task.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/WinHttp/winhttp_http_task.h
    )

set(XMLHttp_HTTP_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/http_buffer.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/http_buffer.h
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/http_request_callback.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/http_request_callback.h
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/http_response_stream.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/http_response_stream.h
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/http_request_stream.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/http_request_stream.h
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/xmlhttp_http_task.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/XMLHttp/xmlhttp_http_task.h
    )

set(Windows_Logger_Source_Files
   ${REL_PATH_TO_PROJECT_DIR}Source/Logger/Win/win_logger.cpp
   )

set(UnitTests_Source_Files_Support
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/DefineTestMacros.h
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/UnitTestIncludes.h
    )

set(Taef_UnitTests_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/TAEF/UnitTestBase.h
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/TAEF/UnitTestBase_winrt.h
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/TAEF/UnitTestIncludes_TAEF.h
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/TAEF/UnitTestBase.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/TAEF/UnitTestBase_winrt.cpp
    )

set(TE_UnitTests_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/TE/unittesthelpers.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/TE/unittesthelpers.h
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Support/TE/unittestincludes_te.h
    )

set(UnitTests_Source_Files_Tests
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/AsyncBlockTests.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/CallbackThunk.h
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/GlobalTests.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/HttpTests.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/LocklessQueueTests.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/MockTests.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/TaskQueueTests.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/WebsocketTests.cpp
    )

include(SetCommonHCSourceFiles.cmake)
set_common_hc_source_files(COMMON_SOURCE_FILES COMMON_INCLUDE_DIRS ${REL_PATH_TO_PROJECT_DIR})

set(SOURCE_FILES ${COMMON_SOURCE_FILES})
include_directories(${COMMON_INCLUDE_DIRS})

if( XDK )
    message(STATUS "XDK source group")
endif()

if( UNITTEST )
    message(STATUS "Unittest source group")
    source_group("C++ Source\\HTTP\\Unittest" FILES ${Unittest_HTTP_Source_Files})
    source_group("C++ Source\\WebSocket\\Unittest" FILES ${Unittest_WebSocket_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})
    source_group("C++ Source\\Logger\\Win" FILES ${Windows_Logger_Source_Files})
    source_group("C++ Source\\Task\\Win" FILES ${Task_Windows_Source_Files})
    list(APPEND
        SOURCE_FILES
        ${Task_Windows_Source_Files}
        ${Unittest_HTTP_Source_Files}
        ${Common_Windows_Source_Files}
        ${Windows_Logger_Source_Files}
        ${Unittest_WebSocket_Source_Files}
        )
endif()

if( UWP )
    message(STATUS "UWP source group")
    source_group("C++ Source\\HTTP\\XMLHttp" FILES ${XMLHttp_HTTP_Source_Files})
    source_group("C++ Source\\WebSocket\\WinRT" FILES ${WinRT_WebSocket_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})
    source_group("C++ Source\\Logger\\Win" FILES ${Windows_Logger_Source_Files})
    source_group("C++ Source\\Task\\Win" FILES ${Task_Windows_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${Task_Windows_Source_Files}
        ${XMLHttp_HTTP_Source_Files}
        ${Common_Windows_Source_Files}
        ${Windows_Logger_Source_Files}
        ${WinRT_WebSocket_Source_Files}
        )
endif()

if( XDK )
    source_group("C++ Source\\HTTP\\XMLHttp" FILES ${XMLHttp_HTTP_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})
    source_group("C++ Source\\WebSocket\\WinRT" FILES ${WinRT_WebSocket_Source_Files})
    source_group("C++ Source\\Logger\\Win" FILES ${Windows_Logger_Source_Files})
    source_group("C++ Source\\Task\\Win" FILES ${Task_Windows_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${Task_Windows_Source_Files}
        ${XMLHttp_HTTP_Source_Files}
        ${Common_Windows_Source_Files}
        ${Windows_Logger_Source_Files}
        ${WinRT_WebSocket_Source_Files}
        )
endif()

if( BUILDWIN32 )
    source_group("C++ Source\\HTTP\\WinHttp" FILES ${WinHttp_HTTP_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})
    source_group("C++ Source\\WebSocket\\Win" FILES ${Win32_WebSocket_Source_Files})
    source_group("C++ Source\\Logger\\Win" FILES ${Windows_Logger_Source_Files})
    source_group("C++ Source\\Task\\Win" FILES ${Task_Windows_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${Task_Windows_Source_Files}
        ${WinHttp_HTTP_Source_Files}
        ${Common_Windows_Source_Files}
        ${Windows_Logger_Source_Files}
        ${Win32_WebSocket_Source_Files}
        )
endif()

if( TAEF )
    message(STATUS "TAEF source group")
    source_group("C++ Source\\UnitTests\\Support" FILES ${UnitTests_Source_Files_Support})
    source_group("C++ Source\\UnitTests\\Tests" FILES ${UnitTests_Source_Files_Tests})
    source_group("C++ Source\\UnitTests\\Support" FILES ${Taef_UnitTests_Source_Files})
    list(APPEND
        SOURCE_FILES
        ${UnitTests_Source_Files_Support}
        ${UnitTests_Source_Files_Tests}
        ${Taef_UnitTests_Source_Files}
        )
endif()

if( TE )
    message(STATUS "TE source group")
    source_group("C++ Source\\UnitTests\\Support" FILES ${UnitTests_Source_Files_Support})
    source_group("C++ Source\\UnitTests\\Tests" FILES ${UnitTests_Source_Files_Tests})
    source_group("C++ Source\\UnitTests\\Support" FILES ${TE_UnitTests_Source_Files})
    list(APPEND
        SOURCE_FILES
        ${UnitTests_Source_Files_Support}
        ${UnitTests_Source_Files_Tests}
        ${TE_UnitTests_Source_Files}
        )
endif()

list( SORT SOURCE_FILES )

add_library(${PROJECT_NAME} "${SOURCE_FILES}")

set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE C)

set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS "/INCREMENTAL:NO")
set(CMAKE_STATIC_LINKER_FLAGS "/INCREMENTAL:NO")

message(STATUS "CMAKE_SYSTEM_VERSION='${CMAKE_SYSTEM_VERSION}'")
message(STATUS "CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'")
message(STATUS "SHORT_VERSION='${SHORT_VERSION}'")
