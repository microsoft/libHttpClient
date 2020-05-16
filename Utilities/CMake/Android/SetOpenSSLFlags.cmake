cmake_minimum_required(VERSION 3.6)

function(SET_OPENSSL_FLAGS TARGET_NAME)
    set(OPENSSL_FLAGS
        "-DOPENSSL_NO_DEVCRYPTOENG"
        "-DDSO_DLFCN"
        "-DHAVE_DLFCN_H"
        "-DNDEBUG"
        "-DOPENSSL_THREADS"
        "-DOPENSSL_NO_STATIC_ENGINE"
        "-D__STDC_NO_ATOMICS__"
        "-DOPENSSL_PIC"
        "-DOPENSSL_USE_NODELETE"
        "-DUNICODE"
        "-DOPENSSLDIR=\"THIS_SHOULD_NOT_BE_USED\""
        "-DENGINESDIR=\"THIS_SHOULD_NOT_BE_USED\""
        )

    foreach(flag ${OPENSSL_FLAGS})
        target_compile_options(${TARGET_NAME} PRIVATE ${flag})
    endforeach()
endfunction()
