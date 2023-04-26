#!/bin/bash
log () {
    echo "***** $1 *****"
}

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

POSITIONAL_ARGS=()

# Default configurations
CONFIGURATION="Release"
BUILD_CURL=true

while [[ $# -gt 0 ]]; do
  case $1 in
    -c|--config)
      CONFIGURATION="$2"
      shift # past argument
      shift # past value
      ;;
    -nc|--nocurl)
      BUILD_CURL=false
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

set -- "${POSITIONAL_ARGS[@]}" # restore positional parameters

log "CONFIGURATION  = ${CONFIGURATION}"
log "BUILD CURL     = ${SEARCHPATH}"

if [ "$BUILD_CURL" = true ]; then
    log "Building cURL"
    ./curl.bash
fi

#make libssl
sudo cmake -S "$SCRIPT_DIR"/../CMake/Linux/openssl/libssl -B "$SCRIPT_DIR"/../../Build/libssl.Linux/build -D CMAKE_BUILD_TYPE=$CONFIGURATION -G "Ninja"
sudo ninja -C "$SCRIPT_DIR"/../../Build/libssl.Linux/build

#make libcrypto
sudo cmake -S "$SCRIPT_DIR"/../CMake/Linux/openssl/libcrypto -B "$SCRIPT_DIR"/../../Build/libcrypto.Linux/build -D CMAKE_BUILD_TYPE=$CONFIGURATION -G "Ninja"
sudo ninja -C "$SCRIPT_DIR"/../../Build/libcrypto.Linux/build

#make libHttpClient
sudo cmake -S "$SCRIPT_DIR"/../CMake/Linux/libHttpClient -B "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build -D CMAKE_BUILD_TYPE=$CONFIGURATION -G "Ninja"
sudo ninja -C "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build