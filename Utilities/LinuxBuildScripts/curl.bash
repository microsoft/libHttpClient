#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
hwclock --hctosys
apt-get install clang
apt-get install make
apt-get install libssl-dev
apt-get install autoconf
apt-get install automake
apt-get install libtool
autoreconf -fi "$SCRIPT_DIR"/../../External/curl
"$SCRIPT_DIR"/../../External/curl/configure
"$SCRIPT_DIR"/../../External/curl/configure --disable-dependency-tracking -with-ssl --enable-symbol-hiding --disable-shared
make
mkdir -p "$SCRIPT_DIR"/../../Binaries/Release/x64/libcurl.Linux
cp -R "$PWD"/lib/.libs/* "$SCRIPT_DIR"/../../Binaries/Release/x64/libcurl.Linux
make clean
