cmake_minimum_required(VERSION 3.6)

function(SET_ANDROID_HC_SOURCE_FILES
         OUT_COMMON_SOURCE_FILES
         OUT_WEBSOCKET_SOURCE_FILES
         OUT_TASK_SOURCE_FILES
         OUT_HTTP_SOURCE_FILES
         OUT_LOGGER_SOURCE_FILES
         PATH_TO_ROOT
         )

    set(${OUT_COMMON_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/Common/Android/utils_android.cpp"
        "${PATH_TO_ROOT}/Source/Common/Android/utils_android.h"
        PARENT_SCOPE
        )

    set(${OUT_WEBSOCKET_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.cpp"
        "${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/x509_cert_utilities.hpp"
        PARENT_SCOPE
        )

    set(${OUT_TASK_SOURCE_FILES}
        "${PATH_TO_ROOT}/Include/httpClient/async_jvm.h"
        "${PATH_TO_ROOT}/Source/Task/ThreadPool_stl.cpp"
        "${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp"
        PARENT_SCOPE
        )

    set(${OUT_HTTP_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/HTTP/Android/http_android.cpp"
        "${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.cpp"
        "${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.h"
        "${PATH_TO_ROOT}/Source/HTTP/Android/android_platform_context.cpp"
        "${PATH_TO_ROOT}/Source/HTTP/Android/android_platform_context.h"
        PARENT_SCOPE
        )

    set(${OUT_LOGGER_SOURCE_FILES}
        "${PATH_TO_ROOT}/Source/Logger/Android/android_logger.cpp"
        PARENT_SCOPE
        )

endfunction()