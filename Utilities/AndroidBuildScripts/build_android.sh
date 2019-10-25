# 2019-10-25
# This is a command-line script for building for Android using the CMake file directly
# Some variables have to be passed to this script as arguments or else defined
# as environment variables.

# Note that this builds libHttpClient assuming that OpenSSL and Crypto have been built first
# Outputs of this script are put in libHttpClient/Binaries/{Debug, Release}

# example command-line for the CMakeLists.txt in ../CMake:
#
# $CMAKE_EXEC -DBUILDANDROID=ON -DCMAKE_TOOLCHAIN_FILE=/Users/jjclose/Library/Android/sdk/ndk/20.0.5594570/build/cmake/android.toolchain.cmake  \
#   -DANDROID_ABI=x86_64 \
#   -DANDROID_NATIVE_API_LEVEL=21 \
#   -DANDROID_TOOLCHAIN=clang \
#   -DANDROID_NDK=${ANDROID_NDK} \
#   -DCMAKE_BUILD_TYPE=Release

# setup paths
workingdir=`pwd`
cmdline=$0

basepath=${cmdline%/*}
fullpath=${workingdir}/${basepath}

libHttpClientRoot=${fullpath}/../..
binariesDir=${libHttpClientRoot}/Binaries
if [ ! -d "${binariesDir}" ]; then
  mkdir -p $binariesDir
fi
for p in Debug Release; do
  if [ ! -d ${binariesDir}/$p ]; then
    mkdir ${binariesDir}/$p
  fi
  for d in ARM ARM64 x64 x86; do
    if [ ! -d ${binariesDir}/$p/$d ]; then
      mkdir ${binariesDir}/$p/$d
    fi
  done
done
