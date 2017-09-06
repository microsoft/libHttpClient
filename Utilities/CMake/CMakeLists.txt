cmake_minimum_required (VERSION 3.6)

if( SHIP )
	set (PROJECT_NAME libHttpClient.Ship)
elseif( UNITTEST )
	if( TAEF )
		set (PROJECT_NAME libHttpClient.UnitTest.140.TAEF)
	elseif( TE )
		set (PROJECT_NAME libHttpClient.UnitTest.140.TE)
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


project (${PROJECT_NAME})

if(MSVC_VERSION GREATER 1909)
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
    $(ProjectDir)
    $(ProjectDir)../../../Source
    $(ProjectDir)../../../Source/Common
    $(ProjectDir)../../../Source/HTTP
    $(ProjectDir)../../../Source/Logger
    $(ProjectDir)../../../include
    )

set(CMAKE_SUPPRESS_REGENERATION true)

set(Public_Source_Files
    ../../../include/httpClient/httpClient.h
    ../../../include/httpClient/trace.h
    ../../../include/httpClient/types.h
    )

set(Common_Source_Files
    ../../../Source/Common/task.cpp
    ../../../Source/Common/task.h
    ../../../Source/Common/mem.cpp
    ../../../Source/Common/mem.h
    ../../../Source/Common/pch.cpp
    ../../../Source/Common/pch.h
    ../../../Source/Common/pch_common.h
    ../../../Source/Common/settings_publics.cpp
    ../../../Source/Common/task_publics.cpp
    ../../../Source/Common/global_publics.cpp
    ../../../Source/Common/singleton.cpp
    ../../../Source/Common/singleton.h
    ../../../Source/Common/utils.cpp
    ../../../Source/Common/utils.h
    ../../../Source/Common/buildver.h
    )

set(Common_Windows_Source_Files
    ../../../Source/Common/Win/utils_win.cpp
    ../../../Source/Common/Win/utils_win.h
    )

set(HTTP_Source_Files
    ../../../Source/HTTP/httpcall.cpp
    ../../../Source/HTTP/httpcall.h
    ../../../Source/HTTP/httpcallrequest.cpp
    ../../../Source/HTTP/httpcallresponse.cpp
    )

set(Mock_HTTP_Source_Files
    ../../../Source/HTTP/Mock/http_mock.cpp
    ../../../Source/HTTP/Mock/http_mock.h
    )

set(Unittest_HTTP_Source_Files
    ../../../Source/HTTP/Unittest/http_unittest.cpp
    )

set(WinRTHttpClient_HTTP_Source_Files
    ../../../Source/HTTP/WinRTHttpClient/http_winrthttpclient.cpp
    )
    
set(XMLHttp_HTTP_Source_Files
    ../../../Source/HTTP/XMLHttp/http_xmlhttp.cpp
    )
    
set(Logger_Source_Files
    ../../../Source/Logger/trace.cpp
    ../../../Source/Logger/trace_internal.h
    )


set(UnitTests_Source_Files_Support
#	../../../Tests/UnitTests/Support/iso8601.cpp
#	../../../Tests/UnitTests/Support/iso8601.h
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
	../../../Tests/UnitTests/Tests/HttpTests.cpp
	../../../Tests/UnitTests/Tests/MockTests.cpp
    ../../../Tests/UnitTests/Tests/GlobalTests.cpp
    ../../../Tests/UnitTests/Tests/TaskTests.cpp
    )

if( SHIP )
	message(STATUS "SHIP add_library")
	# source_group("Build" FILES ${Ship_Source_Files})
	# set( SOURCE_FILES
		# ${Ship_Source_Files}
		# )
endif() # SHIP

message(STATUS "Common source group")
source_group("Header Files" FILES ${Public_Source_Files})
source_group("C++ Source\\Common" FILES ${Common_Source_Files})
source_group("C++ Source\\HTTP" FILES ${HTTP_Source_Files})
source_group("C++ Source\\HTTP\\Mock" FILES ${Mock_HTTP_Source_Files})
source_group("C++ Source\\Logger" FILES ${Logger_Source_Files})

set( SOURCE_FILES
    ${Common_Source_Files}
    ${Public_Source_Files}
    ${Logger_Source_Files}
    ${HTTP_Source_Files}
    ${Mock_HTTP_Source_Files}
    )

if( XDK )
    message(STATUS "XDK source group")
endif()

if( UNITTEST )
    message(STATUS "Unittest source group")
    source_group("C++ Source\\HTTP\\Unittest" FILES ${Unittest_HTTP_Source_Files})
    list(APPEND
        SOURCE_FILES
        ${Unittest_HTTP_Source_Files}
        )
endif()

if( UWP )
    message(STATUS "UWP source group")
    source_group("C++ Source\\HTTP\\WinRTHttpClient" FILES ${WinRTHttpClient_HTTP_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${WinRTHttpClient_HTTP_Source_Files}
        ${Common_Windows_Source_Files}
        )
endif()
    
if( BUILDWIN32 )
    message(STATUS "Win32 source group")
    source_group("C++ Source\\HTTP\\XMLHttp" FILES ${XMLHttp_HTTP_Source_Files})
    source_group("C++ Source\\Common\\Win" FILES ${Common_Windows_Source_Files})

    list(APPEND
        SOURCE_FILES
        ${XMLHttp_HTTP_Source_Files}
        ${Common_Windows_Source_Files}
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
add_library(${PROJECT_NAME} ${SOURCE_FILES})

set_property(TARGET ${PROJECT_NAME} APPEND_STRING PROPERTY LINK_FLAGS "/INCREMENTAL:NO")
set(CMAKE_STATIC_LINKER_FLAGS "/INCREMENTAL:NO")

message(STATUS "CMAKE_SYSTEM_VERSION='${CMAKE_SYSTEM_VERSION}'")
message(STATUS "CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'")
message(STATUS "SHORT_VERSION='${SHORT_VERSION}'")

