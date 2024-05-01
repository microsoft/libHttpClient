# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.18.1)

# Declares and names the project.

project("zlib.Android")

set(CMAKE_STATIC_LIBRARY_PREFIX "")

get_filename_component(PATH_TO_ROOT "../../../../.." ABSOLUTE)

###########################################
### Set up paths for source and include ###
###########################################

set(PATH_TO_EXTERNAL "${PATH_TO_ROOT}/External")
set(PATH_TO_ZLIB "${PATH_TO_EXTERNAL}/zlib")

set(ZLIB_SOURCE_FILES
        "${PATH_TO_ZLIB}/adler32.c"
        "${PATH_TO_ZLIB}/compress.c"
        "${PATH_TO_ZLIB}/crc32.c"
        "${PATH_TO_ZLIB}/deflate.c"
        "${PATH_TO_ZLIB}/gzclose.c"
        "${PATH_TO_ZLIB}/gzlib.c"
        "${PATH_TO_ZLIB}/gzread.c"
        "${PATH_TO_ZLIB}/gzwrite.c"
        "${PATH_TO_ZLIB}/infback.c"
        "${PATH_TO_ZLIB}/inffast.c"
        "${PATH_TO_ZLIB}/inflate.c"
        "${PATH_TO_ZLIB}/inftrees.c"
        "${PATH_TO_ZLIB}/trees.c"
        "${PATH_TO_ZLIB}/uncompr.c"
        "${PATH_TO_ZLIB}/zutil.c"
        "${PATH_TO_ZLIB}/contrib/minizip/unzip.c"
)

set(ZLIB_INCLUDE_DIRS
        "${PATH_TO_ZLIB}"
        "${PATH_TO_ZLIB}/contrib/minizip"
)

#########################
### Set up static lib ###
#########################

add_library( # Sets the name of the library.
        zlib.Android

        # Sets the library as a shared library.
        STATIC

        # Provides a relative path to your source file(s).
        "${ZLIB_SOURCE_FILES}"
)

target_include_directories(
        zlib.Android
        PRIVATE
        "${ZLIB_INCLUDE_DIRS}"
)