cmake_minimum_required(VERSION 3.6)

set(PATH_TO_ROOT "../../..")

#####################################
####### libHttpClient Android #######
#####################################

set(LIBHC_PROJECT_NAME libHttpClient.Android.C)

include(../SetCommonHCSourceFiles.cmake)
set_common_hc_source_files(COMMON_SOURCE_FILES COMMON_INCLUDE_DIRS "${PATH_TO_ROOT}/")

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

add_library(
    ${LIBHC_PROJECT_NAME}
    STATIC
    ${COMMON_SOURCE_FILES}
    ${COMMON_ANDROID_SOURCE_FILES}
    ${TASK_ANDROID_SOURCE_FILES}
    ${ANDROID_HTTP_SOURCE_FILES}
    ${ANDROID_LOGGER_SOURCE_FILES}
    ${ANDROID_WEBSOCKET_SOURCE_FILES}
)

target_include_directories(
    ${LIBHC_PROJECT_NAME}
    PRIVATE
    ${COMMON_INCLUDE_DIRS}
    ${PATH_TO_ROOT}/External/generatedHeaders/android
)

######################
####### libssl #######
######################

set(SSL_PROJECT_NAME ssl)

include(SetSslSourceFiles.cmake)
include(SetOpenSSLFlags.cmake)

set_ssl_source_files(SSL_SOURCE_FILES SSL_INCLUDE_DIRS ${PATH_TO_ROOT})

add_library(${SSL_PROJECT_NAME} STATIC ${SSL_SOURCE_FILES})

target_include_directories(${SSL_PROJECT_NAME} PRIVATE ${SSL_INCLUDE_DIRS})
set_openssl_flags(${SSL_PROJECT_NAME})

#########################
####### libcrypto #######
#########################

set(CRYPTO_PROJECT_NAME crypto)

include(SetCryptoSourceFiles.cmake)
include(SetOpenSSLFlags.cmake)

set_crypto_source_files(CRYPTO_SOURCE_FILES CRYPTO_INCLUDE_DIRS ${PATH_TO_ROOT})

add_library(${CRYPTO_PROJECT_NAME} STATIC ${CRYPTO_SOURCE_FILES})

target_include_directories(${CRYPTO_PROJECT_NAME} PRIVATE ${CRYPTO_INCLUDE_DIRS})
set_openssl_flags(${CRYPTO_PROJECT_NAME})

#########################################
####### Android Gradle workaround #######
#########################################

set(STATIC_LIB_NAMES
    ${LIBHC_PROJECT_NAME}
    ${SSL_PROJECT_NAME}
    ${CRYPTO_PROJECT_NAME}
    )

include(WorkaroundToBuildStaticLibsInGradle.cmake)
workaround_to_build_static_libs_in_gradle("${STATIC_LIB_NAMES}") # Quotes to make sure whole list gets passed
