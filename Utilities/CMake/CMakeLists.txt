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
    elseif( BUILDANDROID )
        set (PROJECT_NAME libHttpClient.Android.C)
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

if (NOT BUILDANDROID)
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
endif()

if (BUILDANDROID)   # needed because the rel path from here (CMAKE) is different than the rel path from the VS Proj files
  set(REL_PATH_TO_PROJECT_DIR "../../")
else()
  set(REL_PATH_TO_PROJECT_DIR "../../../")
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

if (NOT BUILDANDROID)  # no equivalent for these in Clang that I can find
  add_compile_options(/Zm300 /bigobj)
endif()

if (WINDOWS_STORE OR WINDOWS_PHONE)
  add_compile_options(/ZW)
endif()

add_definitions(-D_NO_ASYNCRTIMP -D_NO_PPLXIMP -D_NO_XSAPIIMP -DXSAPI_BUILD)

if (NOT BUILDANDROID)
  set_source_files_properties(${REL_PATH_TO_PROJECT_DIR}Source/Common/pch.cpp PROPERTIES COMPILE_FLAGS "/Ycpch.h")
endif()

if (NOT ${CMAKE_GENERATOR} MATCHES "Visual Studio .*")
  set_property(SOURCE ${REL_PATH_TO_PROJECT_DIR}Source/Common/pch.cpp APPEND PROPERTY OBJECT_OUTPUTS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")
  set_property(SOURCE ${SOURCES} APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pch.pch")
endif()

if (BUILDANDROID)
  set(ProjectDir "${PROJECT_SOURCE_DIR}/")
endif()

include_directories(
    ${ProjectDir}
    ${ProjectDir}${REL_PATH_TO_PROJECT_DIR}Source
    ${ProjectDir}${REL_PATH_TO_PROJECT_DIR}Source/Common
    ${ProjectDir}${REL_PATH_TO_PROJECT_DIR}Source/HTTP
    ${ProjectDir}${REL_PATH_TO_PROJECT_DIR}Source/Logger
    ${ProjectDir}${REL_PATH_TO_PROJECT_DIR}include
    ${ProjectDir}${REL_PATH_TO_PROJECT_DIR}include/httpClient
    ${ProjectDir}${REL_PATH_TO_PROJECT_DIR}External/asio/asio/include
    ${ProjectDir}${REL_PATH_TO_PROJECT_DIR}External/openssl/include
    ${ProjectDir}${REL_PATH_TO_PROJECT_DIR}External/websocketpp
    )

if (BUILDANDROID)
  include_directories(${ProjectDir}${REL_PATH_TO_PROJECT_DIR}External/generatedHeaders/android)
endif()

set(CMAKE_SUPPRESS_REGENERATION true)

set(Public_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}include/httpClient/config.h
    ${REL_PATH_TO_PROJECT_DIR}include/httpClient/httpClient.h
    ${REL_PATH_TO_PROJECT_DIR}include/httpClient/httpProvider.h
    ${REL_PATH_TO_PROJECT_DIR}include/httpClient/mock.h
    ${REL_PATH_TO_PROJECT_DIR}include/xasync.h
    ${REL_PATH_TO_PROJECT_DIR}include/xasyncProvider.h
    ${REL_PATH_TO_PROJECT_DIR}include/xtaskQueue.h
    ${REL_PATH_TO_PROJECT_DIR}include/httpClient/trace.h
    ${REL_PATH_TO_PROJECT_DIR}include/httpClient/pal.h
    ${REL_PATH_TO_PROJECT_DIR}include/httpClient/async.h
    )

set(Task_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/AsyncLib.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/AtomicVector.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/LocklessList.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/referenced_ptr.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/StaticArray.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/TaskQueue.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/TaskQueueImpl.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/TaskQueueP.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/ThreadPool.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/WaitTimer.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/XTaskQueuePriv.h
    )

set(Task_Windows_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/ThreadPool_win32.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/WaitTimer_win32.cpp
    )

set(Task_Android_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Include/httpClient/async_jvm.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/ThreadPool_stl.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Task/WaitTimer_stl.cpp
    )

set(Global_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Global/mem.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Global/mem.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Global/global_publics.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Global/global.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Global/global.h
    )

set(WebSocket_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/hcwebsocket.h
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/hcwebsocket.cpp
    )

set(WinRT_WebSocket_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/WinRT/winrt_websocket.cpp
    )

set(Win32_WebSocket_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/Websocketpp/websocketpp_websocket.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/Websocketpp/x509_cert_utilities.hpp
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/WinHTTP/winhttp_websocket.cpp
    )

set(Android_WebSocket_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/Websocketpp/websocketpp_websocket.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/Websocketpp/x509_cert_utilities.hpp
    )

set(Mock_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Mock/lhc_mock.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Mock/lhc_mock.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Mock/mock_publics.cpp
    )

set(Common_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/buildver.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/EntryList.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/pch.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/pch.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/pch_common.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/pal_internal.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/ResultMacros.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/uri.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/uri.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/utils.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/utils.h
    )

set(Common_Windows_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/Win/utils_win.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/Win/utils_win.h
    )

set(Common_Android_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/Android/utils_android.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Common/Android/utils_android.h
    )

set(HTTP_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/httpcall.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/httpcall.h
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/httpcall_request.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/httpcall_response.cpp
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

set(Android_HTTP_Source_Files
   ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/Android/http_android.cpp
   ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/Android/android_http_request.cpp
   ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/Android/android_http_request.h
   ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/Android/android_platform_context.cpp
   ${REL_PATH_TO_PROJECT_DIR}Source/HTTP/Android/android_platform_context.h
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

set(Logger_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Logger/trace.cpp
    ${REL_PATH_TO_PROJECT_DIR}Source/Logger/trace_internal.h
    ${REL_PATH_TO_PROJECT_DIR}Source/Logger/log_publics.cpp
    )

set(Windows_Logger_Source_Files
   ${REL_PATH_TO_PROJECT_DIR}Source/Logger/Win/win_logger.cpp
   )

set(Android_Logger_Source_Files
    ${REL_PATH_TO_PROJECT_DIR}Source/Logger/Android/android_logger.cpp
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
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/LocklessListTests.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/MockTests.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/TaskQueueTests.cpp
    ${REL_PATH_TO_PROJECT_DIR}Tests/UnitTests/Tests/WebsocketTests.cpp
    )

message(STATUS "Common source group")
source_group("Header Files" FILES ${Public_Source_Files})
source_group("C++ Source\\Common" FILES ${Common_Source_Files})
source_group("C++ Source\\Global" FILES ${Global_Source_Files})
source_group("C++ Source\\WebSocket" FILES ${WebSocket_Source_Files})
source_group("C++ Source\\Task" FILES ${Task_Source_Files})
source_group("C++ Source\\Mock" FILES ${Mock_Source_Files})
source_group("C++ Source\\HTTP" FILES ${HTTP_Source_Files})
source_group("C++ Source\\Logger" FILES ${Logger_Source_Files})

set( SOURCE_FILES
    ${Public_Source_Files}
    ${Common_Source_Files}
    ${Global_Source_Files}
    ${WebSocket_Source_Files}
    ${Task_Source_Files}
    ${Mock_Source_Files}
    ${HTTP_Source_Files}
    ${Logger_Source_Files}
    )

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

if( BUILDANDROID )
    source_group("C++ Source\\HTTP\\Android" FILES ${Android_HTTP_Source_Files})
    source_group("C++ Source\\WebSocket\\Android" FILES ${Android_WebSocket_Source_Files})
    source_group("C++ Source\\Logger\\Android" FILES ${Android_Logger_Source_Files})
    source_group("C++ Source\\Task\\Android" FILES ${Task_Android_Source_Files})
    source_group("C++ Source\\Common\\Android" FILES ${Common_Android_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${Task_Android_Source_Files}
        ${Android_HTTP_Source_Files}
        ${Android_Logger_Source_Files}
        ${Android_WebSocket_Source_Files}
        ${Common_Android_Source_Files}
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

#[[
if (BUILDANDROID)
    set (REL_SOURCE_FILES ${SOURCE_FILES})
    foreach (src_file ${REL_SOURCE_FILES})
        get_filename_component(src_file_abs_path  "${src_file}" ABSOLUTE)
        # message("removing file")
        # message("${src_file}")
        list(REMOVE_ITEM SOURCE_FILES "${src_file}")
        # message("replacing with file")
        # message("${src_file_abs_path}")
        list(APPEND SOURCE_FILES "${src_file_abs_path}")
    endforeach()
endif()
]]

# print_variables()

add_library(${PROJECT_NAME} "${SOURCE_FILES}")

if (BUILDANDROID)
  # Need to build libssl and libcrypto as well for Android.
  # Also need some added logic to work around CMake+Gradle issues.

  set(STATIC_LIB_NAMES ${PROJECT_NAME})

  include(libssl.cmake)
  include(libcrypto.cmake)
  include(gradleStaticLib.cmake)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE C)

if (NOT BUILDANDROID)
  set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS "/INCREMENTAL:NO")
  set(CMAKE_STATIC_LINKER_FLAGS "/INCREMENTAL:NO")
endif()

message(STATUS "CMAKE_SYSTEM_VERSION='${CMAKE_SYSTEM_VERSION}'")
message(STATUS "CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'")
message(STATUS "SHORT_VERSION='${SHORT_VERSION}'")
