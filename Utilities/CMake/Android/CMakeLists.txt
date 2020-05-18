cmake_minimum_required(VERSION 3.6)

set(PATH_TO_ROOT "../../..")

#####################################
####### libHttpClient Android #######
#####################################

set(LIBHC_PROJECT_NAME libHttpClient.Android.C)

include(../SetCommonHCSourceFiles.cmake)
include(SetAndroidHCSourceFiles.cmake)

# HC common source file paths were written without slash b/w path prefix and file name
set_common_hc_source_files(COMMON_SOURCE_FILES COMMON_INCLUDE_DIRS "${PATH_TO_ROOT}/")

set_android_hc_source_files(ANDROID_SOURCE_FILES ANDROID_INCLUDE_DIRS ${PATH_TO_ROOT})

add_library(
    ${LIBHC_PROJECT_NAME}
    STATIC
    ${COMMON_SOURCE_FILES}
    ${ANDROID_SOURCE_FILES}
)

target_include_directories(
    ${LIBHC_PROJECT_NAME}
    PRIVATE
    ${COMMON_INCLUDE_DIRS}
    ${ANDROID_INCLUDE_DIRS}
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
