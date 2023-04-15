#!/bin/bash
echo 
read -p "Enter R for Release " VAR1
VAR2="R"
if [ "$VAR1" = "$VAR2" ]; then
  echo "Release build Starting"

    #make libssl
    mkdir ./../../Build/libssl.Linux/build
    cmake -S ./../CMake/Linux/openssl/libssl -B ./../../Build/libssl.Linux/build -D CMAKE_BUILD_TYPE=Release
    make -C ./../../Build/libssl.Linux/build
    mkdir -p ./../../Binaries/Release/x64/libssl.Linux
    cp ./../../Build/libssl.Linux/build/libssl.Linux.a ./../../Binaries/Release/x64/libssl.Linux
    make -C ./../../Build/libssl.Linux/build clean
    #make libcrypto
    mkdir ./../../Build/libcrypto.Linux/build
    cmake -S ./../CMake/Linux/openssl/libcrypto -B ./../../Build/libcrypto.Linux/build -D CMAKE_BUILD_TYPE=Release
    make -C ./../../Build/libcrypto.Linux/build
    mkdir -p ./../../Binaries/Release/x64/libcrypto.Linux
    cp ./../../Build/libcrypto.Linux/build/libcrypto.Linux.a ./../../Binaries/Release/x64/libcrypto.Linux
    make -C ./../../Build/libcrypto.Linux/build clean
    #make libHttpClient
    mkdir ./../../Build/libHttpClient.Linux.C/build
    cmake -S ./../CMake/Linux/libHttpClient -B ./../../Build/libHttpClient.Linux.C/build -D CMAKE_BUILD_TYPE=Release
    make -C ./../../Build/libHttpClient.Linux.C/build
    mkdir -p ./../../Binaries/Release/x64/libHttpClient.Linux.C
    cp ./../../Build/libHttpClient.Linux.C/build/libHttpClient.Linux.C.a ./../../Binaries/Release/x64/libHttpClient.Linux.C
    make -C ./../../Build/libHttpClient.Linux.C/build clean

else
  echo "Debug build Starting"

    #make libssl
    mkdir ./../../Build/libssl.Linux/build
    cmake -S ./../CMake/Linux/openssl/libssl -B ./../../Build/libssl.Linux/build -D CMAKE_BUILD_TYPE=Debug
    make -C ./../../Build/libssl.Linux/build
    mkdir -p ./../../Binaries/Debug/x64/libssl.Linux
    cp ./../../Build/libssl.Linux/build/libssl.Linux.a ./../../Binaries/Debug/x64/libssl.Linux
    make -C ./../../Build/libssl.Linux/build clean
    #make libcrypto
    mkdir ./../../Build/libcrypto.Linux/build
    cmake -S ./../CMake/Linux/openssl/libcrypto -B ./../../Build/libcrypto.Linux/build -D CMAKE_BUILD_TYPE=Debug
    make -C ./../../Build/libcrypto.Linux/build
    mkdir -p ./../../Binaries/Debug/x64/libcrypto.Linux
    cp ./../../Build/libcrypto.Linux/build/libcrypto.Linux.a ./../../Binaries/Debug/x64/libcrypto.Linux
    make -C ./../../Build/libcrypto.Linux/build clean
    #make libHttpClient
    mkdir ./../../Build/libHttpClient.Linux.C/build
    cmake -S ./../CMake/Linux/libHttpClient -B ./../../Build/libHttpClient.Linux.C/build -D CMAKE_BUILD_TYPE=Debug
    make -C ./../../Build/libHttpClient.Linux.C/build
    mkdir -p ./../../Binaries/Debug/x64/libHttpClient.Linux.C
    cp ./../../Build/libHttpClient.Linux.C/build/libHttpClient.Linux.C.a ./../../Binaries/Debug/x64/libHttpClient.Linux.C
    make -C ./../../Build/libHttpClient.Linux.C/build clean
fi