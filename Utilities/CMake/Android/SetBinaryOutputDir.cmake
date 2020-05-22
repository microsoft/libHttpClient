cmake_minimum_required(VERSION 3.6)

function(SET_BINARY_OUTPUT_DIR PATH_TO_ROOT)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${PATH_TO_ROOT}/Binaries/Android/native/debug/${ANDROID_ABI}" PARENT_SCOPE)
endfunction()