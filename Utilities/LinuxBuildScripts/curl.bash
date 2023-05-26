#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
CONFIGURATION="Release"

while [[ $# -gt 0 ]]; do
  case $1 in
    -c|--config)
      CONFIGURATION="$2"
      shift # past argument
      shift # past value
      ;;
    -*|--*)
      echo "Unknown option $1"
      exit 1
      ;;
    *)
      POSITIONAL_ARGS+=("$1") # save positional arg
      shift # past argument
      ;;
  esac
done

hwclock --hctosys
apt-get install clang
apt-get install make
apt-get install libssl-dev
apt-get install autoconf
apt-get install automake
apt-get install libtool
pushd "$SCRIPT_DIR"/../../External/curl
autoreconf
"$SCRIPT_DIR"/../../External/curl/configure
if [ "$CONFIGURATION" = "Debug" ]; then
    #make libcrypto and libssl
    "$SCRIPT_DIR"/../../External/curl/configure --disable-dependency-tracking -with-ssl --enable-symbol-hiding --disable-shared --enable-debug
else
    #make libcrypto and libssl
    "$SCRIPT_DIR"/../../External/curl/configure --disable-dependency-tracking -with-ssl --enable-symbol-hiding --disable-shared --disable-debug
fi
make clean
make
mkdir -p "$SCRIPT_DIR"/../../Binaries/"$CONFIGURATION"/x64/libcurl.Linux
cp -R "$PWD"/lib/.libs/* "$SCRIPT_DIR"/../../Binaries/"$CONFIGURATION"/x64/libcurl.Linux
make clean
popd