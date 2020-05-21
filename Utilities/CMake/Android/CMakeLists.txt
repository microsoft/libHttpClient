cmake_minimum_required(VERSION 3.6)

set(CMAKE_VERBOSE_MAKEFILE OFF CACHE BOOL "OFF" FORCE)

add_subdirectory("libHttpClient")
add_subdirectory("openssl/libssl")
add_subdirectory("openssl/libcrypto")

set(STATIC_LIB_NAMES
    "libHttpClient.Android.C"
    "ssl"
    "crypto"
    )

include(WorkaroundToBuildStaticLibsInGradle.cmake)
workaround_to_build_static_libs_in_gradle("${STATIC_LIB_NAMES}") # Quotes to make sure whole list gets passed
