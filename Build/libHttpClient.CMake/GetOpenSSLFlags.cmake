cmake_minimum_required(VERSION 3.6)

# This function will set common, debug, and release config compiler flags
# into the three OUT_XXX variables, respectively. These are intended to then
# be passed to `target_set_flags`, from `TargetSetFlags.cmake`.
function(GET_OPENSSL_FLAGS OUT_FLAGS OUT_FLAGS_DEBUG OUT_FLAGS_RELEASE)
    set("${OUT_FLAGS}"
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

    set("${OUT_FLAGS_DEBUG}"
        "-O0"
        "-DDEBUG"
        "-D_DEBUG"
        PARENT_SCOPE
        )

    set("${OUT_FLAGS_RELEASE}"
        "-Os"
        "-DNDEBUG"
        PARENT_SCOPE
        )
endfunction()
