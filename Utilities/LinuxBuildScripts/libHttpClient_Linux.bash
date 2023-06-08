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
      shift
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

# make libcrypto and libssl
sed -i -e 's/\r$//' "$SCRIPT_DIR"/openssl.bash
bash "$SCRIPT_DIR"/openssl.bash -c "$CONFIGURATION"

if [ "$BUILD_CURL" = true ]; then
    log "Building cURL"
    sed -i -e 's/\r$//' "$SCRIPT_DIR"/curl.bash
    bash "$SCRIPT_DIR"/curl.bash -c "$CONFIGURATION"
fi

# make libHttpClient
sudo cmake -S "$SCRIPT_DIR"/../CMake/Linux/libHttpClient -B "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build -D CMAKE_BUILD_TYPE=$CONFIGURATION -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++  -DCMAKE_CXX_FLAGS="-fvisibility=hidden" -DCMAKE_C_FLAGS="-fvisibility=hidden"
sudo make -C "$SCRIPT_DIR"/../../Build/libHttpClient.Linux.C/build
