cmake_minimum_required(VERSION 3.6)

function(GET_LIBHC_BINARY_OUTPUT_DIR PATH_TO_ROOT OUT_BINARY_DIR)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" PATH_FLAVOR)
    set(${OUT_BINARY_DIR} "${PATH_TO_ROOT}/Binaries/Android/native/${PATH_FLAVOR}/${ANDROID_ABI}" PARENT_SCOPE)
endfunction()