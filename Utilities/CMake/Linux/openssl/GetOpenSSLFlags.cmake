cmake_minimum_required(VERSION 3.6)

# This function will set common, debug, and release config compiler flags
# into the three OUT_XXX variables, respectively. These are intended to then
# be passed to `target_set_flags`, from `TargetSetFlags.cmake`.
function(GET_OPENSSL_FLAGS OUT_FLAGS OUT_FLAGS_DEBUG OUT_FLAGS_RELEASE)
    set("${OUT_FLAGS}"
        "-DLINUX"
        "-DUNIX"
        "-DL_ENDIAN"
        "-D_LIB"
        "-DOPENSSLDIR=\"THIS_SHOULD_NOT_BE_USED\""
        "-DENGINESDIR=\"THIS_SHOULD_NOT_BE_USED\""
        "-DOPENSSL_THREADS"
        "-DOPENSSL_NO_DYNAMIC_ENGINE"
        "-DOPENSSL_PIC"
        "-DUNICODE"
        "-D_UNICODE"
        "-DDSO_NONE"
        "-DOPENSSL_NO_DEVCRYPTOENG"
        "-DOPENSSL_NO_AFALGENG"
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
