#!/bin/bash
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
echo "Enter Argument Release for Release "
if [ "$1" = "Release" ]; then
    echo "Release build Starting"
    CONFIGVAR="Release"
else
    echo "Debug build Starting"
    CONFIGVAR="Debug"
fi
#make libssl
cmake -S "$SCRIPT_DIR"/../CMake/Linux/openssl/libssl -B "$SCRIPT_DIR"/../../Build/libssl.Linux/build -D CMAKE_BUILD_TYPE=$CONFIGVAR -G "Ninja"
ninja -C "$SCRIPT_DIR"/../../Build/libssl.Linux/build
#make libcrypto
cmake -S "$SCRIPT_DIR"/../CMake/Linux/openssl/libcrypto -B "$SCRIPT_DIR"/../../Build/libcrypto.Linux/build -D CMAKE_BUILD_TYPE=$CONFIGVAR -G "Ninja"
ninja -C "$SCRIPT_DIR"/../../Build/libcrypto.Linux/build
#make libHttpClient
cmake -S "$SCRIPT_DIR"/../CMake/Linux/libHttpClient -B "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build -D CMAKE_BUILD_TYPE=$CONFIGVAR -G "Ninja"
ninja -C "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build