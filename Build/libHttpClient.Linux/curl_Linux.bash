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

if [ -f "$SCRIPT_DIR/../../Out/x64/$CONFIGURATION/libcurl.Linux/libcurl.a" ]; then
  echo "Previously-built library present at $SCRIPT_DIR/../../Out/x64/$CONFIGURATION/libcurl.Linux/libcurl.a - skipping build"
  exit 0
else
  echo "No previously-built library present at $SCRIPT_DIR/../../Out/x64/$CONFIGURATION/libcurl.Linux/libcurl.a - performing build"
fi

if [ "$CONFIGURATION" = "Debug" ]; then
    # make libcrypto and libssl
    ./configure --disable-shared --with-zlib --disable-dependency-tracking -with-openssl=/usr/local/ssl --enable-symbol-hiding --enable-debug --without-brotli
else
    # make libcrypto and libssl
    ./configure --disable-shared --with-zlib --disable-dependency-tracking -with-openssl=/usr/local/ssl --enable-symbol-hiding --disable-debug --without-brotli
fi

make

# copies binaries to final directory
mkdir -p "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcurl.Linux
cp -R "$PWD"/lib/.libs/* "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcurl.Linux

make clean
popd
