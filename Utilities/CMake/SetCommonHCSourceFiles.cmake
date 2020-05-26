cmake_minimum_required(VERSION 3.6)

function(SET_COMMON_HC_SOURCE_FILES OUT_SOURCE_FILES OUT_INCLUDE_DIRS PATH_TO_ROOT)
    set(PUBLIC_SOURCE_FILES
        "${PATH_TO_ROOT}/Include/httpClient/config.h"
        "${PATH_TO_ROOT}/Include/httpClient/httpClient.h"
        "${PATH_TO_ROOT}/Include/httpClient/httpProvider.h"
        "${PATH_TO_ROOT}/Include/httpClient/mock.h"
        "${PATH_TO_ROOT}/Include/xasync.h"
        "${PATH_TO_ROOT}/Include/xasyncProvider.h"
        "${PATH_TO_ROOT}/Include/xtaskQueue.h"
        "${PATH_TO_ROOT}/Include/httpClient/trace.h"
        "${PATH_TO_ROOT}/Include/httpClient/pal.h"
        "${PATH_TO_ROOT}/Include/httpClient/async.h"
        )

    set(COMMON_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/Common/buildver.h"
        "${PATH_TO_ROOT}/Source/Common/EntryList.h"
        "${PATH_TO_ROOT}/Source/Common/pch.cpp"
        "${PATH_TO_ROOT}/Source/Common/pch.h"
        "${PATH_TO_ROOT}/Source/Common/pch_common.h"
        "${PATH_TO_ROOT}/Source/Common/pal_internal.h"
        "${PATH_TO_ROOT}/Source/Common/ResultMacros.h"
        "${PATH_TO_ROOT}/Source/Common/uri.cpp"
        "${PATH_TO_ROOT}/Source/Common/uri.h"
        "${PATH_TO_ROOT}/Source/Common/utils.cpp"
        "${PATH_TO_ROOT}/Source/Common/utils.h"
        )

    set(GLOBAL_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/Global/mem.cpp"
        "${PATH_TO_ROOT}/Source/Global/mem.h"
        "${PATH_TO_ROOT}/Source/Global/global_publics.cpp"
        "${PATH_TO_ROOT}/Source/Global/global.cpp"
        "${PATH_TO_ROOT}/Source/Global/global.h"
        )

    set(WEBSOCKET_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/WebSocket/hcwebsocket.h"
        "${PATH_TO_ROOT}/Source/WebSocket/hcwebsocket.cpp"
        )

    set(TASK_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/Task/AsyncLib.cpp"
        "${PATH_TO_ROOT}/Source/Task/AtomicVector.h"
        "${PATH_TO_ROOT}/Source/Task/LocklessQueue.h"
        "${PATH_TO_ROOT}/Source/Task/referenced_ptr.h"
        "${PATH_TO_ROOT}/Source/Task/StaticArray.h"
        "${PATH_TO_ROOT}/Source/Task/TaskQueue.cpp"
        "${PATH_TO_ROOT}/Source/Task/TaskQueueImpl.h"
        "${PATH_TO_ROOT}/Source/Task/TaskQueueP.h"
        "${PATH_TO_ROOT}/Source/Task/ThreadPool.h"
        "${PATH_TO_ROOT}/Source/Task/WaitTimer.h"
        "${PATH_TO_ROOT}/Source/Task/XTaskQueuePriv.h"
        )

    set(MOCK_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/Mock/lhc_mock.cpp"
        "${PATH_TO_ROOT}/Source/Mock/lhc_mock.h"
        "${PATH_TO_ROOT}/Source/Mock/mock_publics.cpp"
        )

    set(HTTP_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/HTTP/httpcall.cpp"
        "${PATH_TO_ROOT}/Source/HTTP/httpcall.h"
        "${PATH_TO_ROOT}/Source/HTTP/httpcall_request.cpp"
        "${PATH_TO_ROOT}/Source/HTTP/httpcall_response.cpp"
        )

    set(LOGGER_SOURCE_FILES
        "${PATH_TO_ROOT}/Source/Logger/trace.cpp"
        "${PATH_TO_ROOT}/Source/Logger/trace_internal.h"
        "${PATH_TO_ROOT}/Source/Logger/log_publics.cpp"
        )

    message(STATUS "Common source group")
    source_group("Header Files" FILES "${PUBLIC_SOURCE_FILES}")
    source_group("C++ Source\\Common" FILES "${COMMON_SOURCE_FILES}")
    source_group("C++ Source\\Global" FILES "${GLOBAL_SOURCE_FILES}")
    source_group("C++ Source\\WebSocket" FILES "${WEBSOCKET_SOURCE_FILES}")
    source_group("C++ Source\\Task" FILES "${TASK_SOURCE_FILES}")
    source_group("C++ Source\\Mock" FILES "${MOCK_SOURCE_FILES}")
    source_group("C++ Source\\HTTP" FILES "${HTTP_SOURCE_FILES}")
    source_group("C++ Source\\Logger" FILES "${LOGGER_SOURCE_FILES}")

    set(${OUT_INCLUDE_DIRS}
        "${PATH_TO_ROOT}/Source"
        "${PATH_TO_ROOT}/Source/Common"
        "${PATH_TO_ROOT}/Source/HTTP"
        "${PATH_TO_ROOT}/Source/Logger"
        "${PATH_TO_ROOT}/Include"
        "${PATH_TO_ROOT}/Include/httpClient"
        "${PATH_TO_ROOT}/External/asio/asio/include"
        "${PATH_TO_ROOT}/External/openssl/include"
        "${PATH_TO_ROOT}/External/websocketpp"
        PARENT_SCOPE
        )

    set(${OUT_SOURCE_FILES}
        "${PUBLIC_SOURCE_FILES}"
        "${COMMON_SOURCE_FILES}"
        "${GLOBAL_SOURCE_FILES}"
        "${WEBSOCKET_SOURCE_FILES}"
        "${TASK_SOURCE_FILES}"
        "${MOCK_SOURCE_FILES}"
        "${HTTP_SOURCE_FILES}"
        "${LOGGER_SOURCE_FILES}"
        PARENT_SCOPE
        )

endfunction()
