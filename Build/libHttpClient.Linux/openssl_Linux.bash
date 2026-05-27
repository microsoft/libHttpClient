#!/bin/bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
OPENSSL_SRC="$SCRIPT_DIR/../../External/openssl"
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

if [ -f "$SCRIPT_DIR/../../Out/x64/$CONFIGURATION/libcrypto.Linux/libcrypto.a" ]; then
  echo "Previously-built library present at $SCRIPT_DIR/../../Out/x64/$CONFIGURATION/libcrypto.Linux/libcrypto.a - skipping build"
  exit 0
else
  echo "No previously-built library present at $SCRIPT_DIR/../../Out/x64/$CONFIGURATION/libcrypto.Linux/libcrypto.a - performing build"
fi

OPENSSL_INSTALL_DIR="$SCRIPT_DIR/../../Int/x64/$CONFIGURATION/openssl.Linux/"

mkdir -p "$OPENSSL_INSTALL_DIR"
mkdir -p "$OPENSSL_INSTALL_DIR/lib"
mkdir -p "$OPENSSL_INSTALL_DIR/include"
mkdir -p "$OPENSSL_INSTALL_DIR/include/openssl"

if [ ! -d "$OPENSSL_INSTALL_DIR" ] ; then
    echo "Directory $OPENSSL_INSTALL_DIR does not exist"
    exit 1
fi
if [ ! -d "$OPENSSL_INSTALL_DIR/lib" ] ; then
    echo "Directory $OPENSSL_INSTALL_DIR/lib does not exist"
    exit 1
fi
if [ ! -d "$OPENSSL_INSTALL_DIR/include/openssl" ] ; then
    echo "Directory $OPENSSL_INSTALL_DIR/include/openssl does not exist"
    exit 1
fi

pushd $OPENSSL_SRC
if [ -f Makefile ]; then
    echo "Cleaning previous OpenSSL build"
    make clean
fi
sed -i -e 's/\r$//' Configure

CONFIGURE_ARGS=(
  --prefix=$OPENSSL_INSTALL_DIR
  --openssldir=$OPENSSL_INSTALL_DIR
  linux-x86_64-clang
  no-shared
  no-hw
  no-tests
  )
if [ "$CONFIGURATION" = "Debug" ]; then
    CONFIGURE_ARGS+=(-d)
fi

# make libcrypto and libssl
./Configure "${CONFIGURE_ARGS[@]}"

MAKE_PARALLELISM="-j$(nproc)" # run Make in parallel to speed up the build process
make $MAKE_PARALLELISM CFLAGS="-fvisibility=hidden" CXXFLAGS="-fvisibility=hidden"
make install_sw
# copies binaries to final directory
mkdir -p "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcrypto.Linux
cp -R "$PWD"/libcrypto.a "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libcrypto.Linux

mkdir -p "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libssl.Linux
cp -R "$PWD"/libssl.a "$SCRIPT_DIR"/../../Out/x64/"$CONFIGURATION"/libssl.Linux

make clean
popd
