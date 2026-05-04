#!/bin/bash

set -e

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

if [ -f "$SCRIPT_DIR/../../Out/x64/$CONFIGURATION/libcurl.Linux/libcurl.a" ]; then
  echo "Previously-built library present at $SCRIPT_DIR/../../Out/x64/$CONFIGURATION/libcurl.Linux/libcurl.a - skipping build"
  exit 0
else
  echo "No previously-built library present at $SCRIPT_DIR/../../Out/x64/$CONFIGURATION/libcurl.Linux/libcurl.a - performing build"
fi

pushd "$SCRIPT_DIR"/../../External/curl
autoreconf -fi "$SCRIPT_DIR"/../../External/curl

OPENSSL_INSTALL_DIR="$SCRIPT_DIR/../../Int/x64/$CONFIGURATION/openssl.Linux/"

CONFIGURE_ARGS=(
    --disable-shared
    --with-zlib
    --disable-dependency-tracking
    --with-openssl=$OPENSSL_INSTALL_DIR
    --enable-symbol-hiding
    --without-brotli
    --without-zstd
)
if [ "$CONFIGURATION" = "Debug" ]; then
    CONFIGURE_ARGS+=(--enable-debug)
else
    CONFIGURE_ARGS+=(--disable-debug)
fi

# make libcrypto and libssl
./configure "${CONFIGURE_ARGS[@]}"

MAKE_PARALLELISM="-j$(nproc)" # run Make in parallel to speed up the build process
make $MAKE_PARALLELISM

# copies binaries to final directory
mkdir -p "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcurl.Linux
rm -f "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcurl.Linux/libcurl.a
rm -f "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcurl.Linux/libcurl.la
rm -f "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcurl.Linux/libcurl.lai
cp "$PWD"/lib/.libs/libcurl.a "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcurl.Linux/libcurl.a

make clean
popd
