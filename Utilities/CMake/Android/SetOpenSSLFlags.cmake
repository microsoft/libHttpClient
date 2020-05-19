cmake_minimum_required(VERSION 3.6)

function(SET_OPENSSL_FLAGS TARGET_NAME)
    set(OPENSSL_FLAGS
        "-Wall"
        "-DOPENSSL_NO_DEVCRYPTOENG"
        "-DDSO_DLFCN"
        "-DHAVE_DLFCN_H"
        "-DOPENSSL_THREADS"
        "-DOPENSSL_NO_STATIC_ENGINE"
        "-D__STDC_NO_ATOMICS__"
        "-DOPENSSL_PIC"
        "-DOPENSSL_USE_NODELETE"
        "-DUNICODE"
        "-DOPENSSLDIR=\"THIS_SHOULD_NOT_BE_USED\""
        "-DENGINESDIR=\"THIS_SHOULD_NOT_BE_USED\""
        )

    set(OPENSSL_FLAGS_DEBUG
        "-O0"
        "-DDEBUG"
        "-D_DEBUG"
        )

    set(OPENSSL_FLAGS_RELEASE
        "-Os"
        "-DNDEBUG"
        )

    foreach(flag ${OPENSSL_FLAGS})
        target_compile_options(${TARGET_NAME} PRIVATE ${flag})
    endforeach()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        foreach(flag ${OPENSSL_FLAGS_DEBUG})
            target_compile_options(${TARGET_NAME} PRIVATE ${flag})
        endforeach()
    elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
        foreach(flag ${OPENSSL_FLAGS_RELEASE})
            target_compile_options(${TARGET_NAME} PRIVATE ${flag})
        endforeach()
    endif()
endfunction()
