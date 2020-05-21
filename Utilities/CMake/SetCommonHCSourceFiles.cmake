cmake_minimum_required(VERSION 3.6)

function(SET_COMMON_HC_SOURCE_FILES OUT_SOURCE_FILES OUT_INCLUDE_DIRS REL_PATH_TO_PROJECT_DIR)
    set(PUBLIC_SOURCE_FILES
        "${REL_PATH_TO_PROJECT_DIR}include/httpClient/config.h"
        "${REL_PATH_TO_PROJECT_DIR}include/httpClient/httpClient.h"
        "${REL_PATH_TO_PROJECT_DIR}include/httpClient/httpProvider.h"
        "${REL_PATH_TO_PROJECT_DIR}include/httpClient/mock.h"
        "${REL_PATH_TO_PROJECT_DIR}include/xasync.h"
        "${REL_PATH_TO_PROJECT_DIR}include/xasyncProvider.h"
        "${REL_PATH_TO_PROJECT_DIR}include/xtaskQueue.h"
        "${REL_PATH_TO_PROJECT_DIR}include/httpClient/trace.h"
        "${REL_PATH_TO_PROJECT_DIR}include/httpClient/pal.h"
        "${REL_PATH_TO_PROJECT_DIR}include/httpClient/async.h"
        )

    set(COMMON_SOURCE_FILES
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/buildver.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/EntryList.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/pch.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/pch.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/pch_common.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/pal_internal.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/ResultMacros.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/uri.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/uri.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/utils.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common/utils.h"
        )

    set(GLOBAL_SOURCE_FILES
        "${REL_PATH_TO_PROJECT_DIR}Source/Global/mem.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Global/mem.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Global/global_publics.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Global/global.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Global/global.h"
        )

    set(WEBSOCKET_SOURCE_FILES
        "${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/hcwebsocket.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/WebSocket/hcwebsocket.cpp"
        )

    set(TASK_SOURCE_FILES
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/AsyncLib.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/AtomicVector.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/LocklessQueue.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/referenced_ptr.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/StaticArray.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/TaskQueue.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/TaskQueueImpl.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/TaskQueueP.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/ThreadPool.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/WaitTimer.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Task/XTaskQueuePriv.h"
        )

    set(MOCK_SOURCE_FILES
        "${REL_PATH_TO_PROJECT_DIR}Source/Mock/lhc_mock.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Mock/lhc_mock.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Mock/mock_publics.cpp"
        )

    set(HTTP_SOURCE_FILES
        "${REL_PATH_TO_PROJECT_DIR}Source/HTTP/httpcall.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/HTTP/httpcall.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/HTTP/httpcall_request.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/HTTP/httpcall_response.cpp"
        )

    set(LOGGER_SOURCE_FILES
        "${REL_PATH_TO_PROJECT_DIR}Source/Logger/trace.cpp"
        "${REL_PATH_TO_PROJECT_DIR}Source/Logger/trace_internal.h"
        "${REL_PATH_TO_PROJECT_DIR}Source/Logger/log_publics.cpp"
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
        "${REL_PATH_TO_PROJECT_DIR}Source"
        "${REL_PATH_TO_PROJECT_DIR}Source/Common"
        "${REL_PATH_TO_PROJECT_DIR}Source/HTTP"
        "${REL_PATH_TO_PROJECT_DIR}Source/Logger"
        "${REL_PATH_TO_PROJECT_DIR}include"
        "${REL_PATH_TO_PROJECT_DIR}include/httpClient"
        "${REL_PATH_TO_PROJECT_DIR}External/asio/asio/include"
        "${REL_PATH_TO_PROJECT_DIR}External/openssl/include"
        "${REL_PATH_TO_PROJECT_DIR}External/websocketpp"
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
