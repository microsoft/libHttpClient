cmake_minimum_required(VERSION 3.6)

function(GET_COMMON_HC_SOURCE_FILES
         OUT_PUBLIC_SOURCE_FILES
         OUT_COMMON_SOURCE_FILES
         OUT_GLOBAL_SOURCE_FILES
         OUT_WEBSOCKET_SOURCE_FILES
         OUT_TASK_SOURCE_FILES
         OUT_MOCK_SOURCE_FILES
         OUT_HTTP_SOURCE_FILES
         OUT_LOGGER_SOURCE_FILES
         PATH_TO_ROOT
         )

    set(${OUT_PUBLIC_SOURCE_FILES}
        "${PATH_TO_ROOT}/include/httpClient/config.h"
        "${PATH_TO_ROOT}/include/httpClient/httpClient.h"
        "${PATH_TO_ROOT}/include/httpClient/httpProvider.h"
        "${PATH_TO_ROOT}/include/httpClient/mock.h"
        "${PATH_TO_ROOT}/include/xasync.h"
        "${PATH_TO_ROOT}/include/xasyncProvider.h"
        "${PATH_TO_ROOT}/include/xtaskQueue.h"
        "${PATH_TO_ROOT}/include/httpClient/trace.h"
        "${PATH_TO_ROOT}/include/httpClient/pal.h"
        "${PATH_TO_ROOT}/include/httpClient/async.h"
        PARENT_SCOPE
        )

    set(${OUT_COMMON_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/Common/buildver.h"
        "${PATH_TO_ROOT}/Source/Common/EntryList.h"
        "${PATH_TO_ROOT}/Source/Common/pch.cpp"
        "${PATH_TO_ROOT}/Source/Common/pch.h"
        "${PATH_TO_ROOT}/Source/Common/pch_common.h"
        "${PATH_TO_ROOT}/Source/Common/pal_internal.h"
        "${PATH_TO_ROOT}/Source/Common/Result.h"
        "${PATH_TO_ROOT}/Source/Common/ResultMacros.h"
        "${PATH_TO_ROOT}/Source/Common/uri.cpp"
        "${PATH_TO_ROOT}/Source/Common/uri.h"
        "${PATH_TO_ROOT}/Source/Common/utils.cpp"
        "${PATH_TO_ROOT}/Source/Common/utils.h"
        PARENT_SCOPE
        )

    set(${OUT_GLOBAL_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/Global/mem.cpp"
        "${PATH_TO_ROOT}/Source/Global/mem.h"
        "${PATH_TO_ROOT}/Source/Global/global_publics.cpp"
        "${PATH_TO_ROOT}/Source/Global/global.cpp"
        "${PATH_TO_ROOT}/Source/Global/global.h"
        PARENT_SCOPE
        )

    set(${OUT_WEBSOCKET_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/WebSocket/hcwebsocket.h"
        "${PATH_TO_ROOT}/Source/WebSocket/hcwebsocket.cpp"
        PARENT_SCOPE
        )

    set(${OUT_TASK_SOURCE_FILES}
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
        "${PATH_TO_ROOT}/Source/Task/XAsyncProviderPriv.h"
        PARENT_SCOPE
        )

    set(${OUT_MOCK_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/Mock/lhc_mock.cpp"
        "${PATH_TO_ROOT}/Source/Mock/lhc_mock.h"
        "${PATH_TO_ROOT}/Source/Mock/mock_publics.cpp"
        PARENT_SCOPE
        )

    set(${OUT_HTTP_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/HTTP/httpcall.cpp"
        "${PATH_TO_ROOT}/Source/HTTP/httpcall.h"
        "${PATH_TO_ROOT}/Source/HTTP/httpcall_request.cpp"
        "${PATH_TO_ROOT}/Source/HTTP/httpcall_response.cpp"
        PARENT_SCOPE
        )

    set(${OUT_LOGGER_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/Logger/trace.cpp"
        "${PATH_TO_ROOT}/Source/Logger/trace_internal.h"
        "${PATH_TO_ROOT}/Source/Logger/log_publics.cpp"
        PARENT_SCOPE
        )

endfunction()
