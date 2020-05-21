cmake_minimum_required(VERSION 3.6)

function(GET_OPENSSL_FLAGS FLAGS FLAGS_DEBUG FLAGS_RELEASE)
    set(FLAGS
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
        PARENT_SCOPE
        )

    set(FLAGS_DEBUG
        "-O0"
        "-DDEBUG"
        "-D_DEBUG"
        PARENT_SCOPE
        )

    set(FLAGS_RELEASE
        "-Os"
        "-DNDEBUG"
        PARENT_SCOPE
        )
endfunction()
