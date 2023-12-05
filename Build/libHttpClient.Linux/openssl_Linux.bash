#!/bin/bash

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

sudo hwclock --hctosys
sudo rm -rf /usr/local/ssl
sudo mkdir /usr/local/ssl
sudo mkdir /usr/local/ssl/lib
sudo mkdir /usr/local/ssl/include
sudo mkdir /usr/local/ssl/include/openssl

if [ ! -d /usr/local/ssl ] ; then 
    echo "Directory /usr/local/ssl does not exist"
    exit 1
fi
if [ ! -d /usr/local/ssl/lib ] ; then 
    echo "Directory /usr/local/ssl/lib does not exist"
    exit 1
fi
if [ ! -d /usr/local/ssl/include/openssl ] ; then 
    echo "Directory /usr/local/ssl/include/openssl does not exist"
    exit 1
fi

pushd $OPENSSL_SRC
make clean
sed -i -e 's/\r$//' Configure

if [ "$CONFIGURATION" = "Debug" ]; then
    # make libcrypto and libssl
    ./Configure --prefix=/usr/local/ssl --openssldir=/usr/local/ssl linux-x86_64-clang no-shared no-hw no-engine no-async -d
else
    # make libcrypto and libssl
    ./Configure --prefix=/usr/local/ssl --openssldir=/usr/local/ssl linux-x86_64-clang no-shared no-hw no-engine no-async
fi

make CFLAGS="-fvisibility=hidden" CXXFLAGS="-fvisibility=hidden"
sudo make install
# copies binaries to final directory
mkdir -p "$SCRIPT_DIR"/../../Binaries/"$CONFIGURATION"/x64/libcrypto.Linux
cp -R "$PWD"/libcrypto.a "$SCRIPT_DIR"/../../Binaries/"$CONFIGURATION"/x64/libcrypto.Linux

mkdir -p "$SCRIPT_DIR"/../../Binaries/"$CONFIGURATION"/x64/libssl.Linux
cp -R "$PWD"/libssl.a "$SCRIPT_DIR"/../../Binaries/"$CONFIGURATION"/x64/libssl.Linux

make clean
popd
