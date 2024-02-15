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

pushd "$SCRIPT_DIR"/../../External/curl
autoreconf -fi "$SCRIPT_DIR"/../../External/curl

if [ "$CONFIGURATION" = "Debug" ]; then
    # make libcrypto and libssl
    ./configure --disable-shared --disable-dependency-tracking -with-openssl=/usr/local/ssl --enable-symbol-hiding --enable-debug
else
    # make libcrypto and libssl
    ./configure --disable-shared --disable-dependency-tracking -with-openssl=/usr/local/ssl --enable-symbol-hiding --disable-debug
fi

make

# copies binaries to final directory
mkdir -p "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcurl.Linux
cp -R "$PWD"/lib/.libs/* "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcurl.Linux

make clean
popd
