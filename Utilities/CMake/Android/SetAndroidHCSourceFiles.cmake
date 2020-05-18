cmake_minimum_required(VERSION 3.6)

function(SET_ANDROID_HC_SOURCE_FILES OUT_SOURCE_FILES OUT_INCLUDE_DIRS PATH_TO_ROOT)

    set(COMMON_ANDROID_SOURCE_FILES
        ${PATH_TO_ROOT}/Source/Common/Android/utils_android.cpp
        ${PATH_TO_ROOT}/Source/Common/Android/utils_android.h
        )

    set(TASK_ANDROID_SOURCE_FILES
        ${PATH_TO_ROOT}/Include/httpClient/async_jvm.h
        ${PATH_TO_ROOT}/Source/Task/ThreadPool_stl.cpp
        ${PATH_TO_ROOT}/Source/Task/WaitTimer_stl.cpp
        )

    set(ANDROID_HTTP_SOURCE_FILES
        ${PATH_TO_ROOT}/Source/HTTP/Android/http_android.cpp
        ${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.cpp
        ${PATH_TO_ROOT}/Source/HTTP/Android/android_http_request.h
        ${PATH_TO_ROOT}/Source/HTTP/Android/android_platform_context.cpp
        ${PATH_TO_ROOT}/Source/HTTP/Android/android_platform_context.h
        )

    set(ANDROID_LOGGER_SOURCE_FILES
        ${PATH_TO_ROOT}/Source/Logger/Android/android_logger.cpp
        )

    set(ANDROID_WEBSOCKET_SOURCE_FILES
        ${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/websocketpp_websocket.cpp
        ${PATH_TO_ROOT}/Source/WebSocket/Websocketpp/x509_cert_utilities.hpp
        )

    set(${ANDROID_INCLUDE_DIRS} ${PATH_TO_ROOT}/External/generatedHeaders/android)

    set(${OUT_SOURCE_FILES}
        ${COMMON_ANDROID_SOURCE_FILES}
        ${TASK_ANDROID_SOURCE_FILES}
        ${ANDROID_HTTP_SOURCE_FILES}
        ${ANDROID_LOGGER_SOURCE_FILES}
        ${ANDROID_WEBSOCKET_SOURCE_FILES}
        PARENT_SCOPE
        )

    set(${OUT_INCLUDE_DIRS}
        ${ANDROID_INCLUDE_DIRS}
        PARENT_SCOPE
        )

endfunction()