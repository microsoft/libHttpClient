cmake_minimum_required(VERSION 3.6)

function(SET_HC_FLAGS TARGET_NAME)
    set(HC_FLAGS
        "-Wall"
        "-fexceptions"
        "-std=c++14"
        "-Wno-unknown-pragmas"
        "-Wno-pragma-once-outside-header"
        "-rtti"
        "-DHC_PLATFORM_MSBUILD_GUESS=HC_PLATFORM_ANDROID"
        )

    set(HC_FLAGS_DEBUG
        "-O0"
        "-DHC_TRACE_BUILD_LEVEL=5"
        )

    set(HC_FLAGS_RELEASE
        "-Os"
        "-DHC_TRACE_BUILD_LEVEL=3"
        )

    foreach(flag ${HC_FLAGS})
        target_compile_options(${TARGET_NAME} PRIVATE ${flag})
    endforeach()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        foreach(flag ${HC_FLAGS_DEBUG})
            target_compile_options(${TARGET_NAME} PRIVATE ${flag})
        endforeach()
    elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
        foreach(flag ${HC_FLAGS_RELEASE})
            target_compile_options(${TARGET_NAME} PRIVATE ${flag})
        endforeach()
    endif()
endfunction()
