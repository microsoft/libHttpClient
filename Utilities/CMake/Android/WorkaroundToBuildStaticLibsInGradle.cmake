cmake_minimum_required(VERSION 3.6)

function(WORKAROUND_TO_BUILD_STATIC_LIBS_IN_GRADLE STATIC_LIB_NAMES)
    # Gradle's (and therefore Android Studio's) invocation of CMake
    # won't actually produce a built library if the library type is
    # STATIC (the default), only if it is SHARED. (Since a static lib
    # is of limited use typically to an Android app.)
    #
    # This is a workaround to force creation of a static lib even
    # from Gradle. See https://stackoverflow.com/questions/52952226.

    set(DUMMY_ANDROID_PROJECT_NAME "dummyAndroidProjectName")
    add_library("${DUMMY_ANDROID_PROJECT_NAME}" SHARED)
    set_target_properties("${DUMMY_ANDROID_PROJECT_NAME}" PROPERTIES LINKER_LANGUAGE CXX)

    target_link_libraries("${DUMMY_ANDROID_PROJECT_NAME}" "${STATIC_LIB_NAMES}")
endfunction()