#!/bin/bash
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
read -p "Enter R for Release " INPUTVAR
if [ "$INPUTVAR" = "R" ]; then
    echo "Release build Starting"
    CONFIGVAR="Release"
else
    echo "Debug build Starting"
    CONFIGVAR="Debug"
fi
#make libssl
mkdir "$SCRIPT_DIR"/../../Build/libssl.Linux/build
cmake -S "$SCRIPT_DIR"/../CMake/Linux/openssl/libssl -B "$SCRIPT_DIR"/../../Build/libssl.Linux/build -D CMAKE_BUILD_TYPE="$CONFIGVAR"
make -C "$SCRIPT_DIR"/../../Build/libssl.Linux/build
mkdir -p "$SCRIPT_DIR"/../../Binaries/"$CONFIGVAR"/x64/libssl.Linux
cp "$SCRIPT_DIR"/../../Build/libssl.Linux/build/libssl.Linux.a "$SCRIPT_DIR"/../../Binaries/"$CONFIGVAR"/x64/libssl.Linux
make -C "$SCRIPT_DIR"/../../Build/libssl.Linux/build clean
#make libcrypto
mkdir "$SCRIPT_DIR"/../../Build/libcrypto.Linux/build
cmake -S "$SCRIPT_DIR"/../CMake/Linux/openssl/libcrypto -B "$SCRIPT_DIR"/../../Build/libcrypto.Linux/build -D CMAKE_BUILD_TYPE="$CONFIGVAR"
make -C "$SCRIPT_DIR"/../../Build/libcrypto.Linux/build
mkdir -p "$SCRIPT_DIR"/../../Binaries/"$CONFIGVAR"/x64/libcrypto.Linux
cp "$SCRIPT_DIR"/../../Build/libcrypto.Linux/build/libcrypto.Linux.a "$SCRIPT_DIR"/../../Binaries/"$CONFIGVAR"/x64/libcrypto.Linux
make -C "$SCRIPT_DIR"/../../Build/libcrypto.Linux/build clean
#make libHttpClient
mkdir "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build
cmake -S "$SCRIPT_DIR"/../CMake/Linux/libHttpClient -B "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build -D CMAKE_BUILD_TYPE="$CONFIGVAR"
make -C "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build
mkdir -p "$SCRIPT_DIR"/../../Binaries/"$CONFIGVAR"/x64/libHttpClient.Linux.C
cp "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build/libHttpClient.Linux.C.a "$SCRIPT_DIR"/../../Binaries/"$CONFIGVAR"/x64/libHttpClient.Linux.C
make -C "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build clean