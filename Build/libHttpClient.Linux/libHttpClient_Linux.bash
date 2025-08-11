#!/bin/bash
log () {
    echo "***** $1 *****"
}

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

POSITIONAL_ARGS=()

# Default configurations
CONFIGURATION="Release"
BUILD_CURL=true
BUILD_SSL=true
BUILD_STATIC=false

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
    -ns|--nossl)
      BUILD_SSL=false
      shift
      ;;
    -sg|--skipaptget)
      DO_APTGET=false
      shift
      ;;
    -st|--static)
      BUILD_STATIC=true
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

if [ "$DO_APTGET" != "false" ]; then
  sudo hwclock --hctosys
  sudo apt-get update
  sudo apt-get install clang
  sudo apt-get install make
  sudo apt-get install autoconf
  sudo apt-get install automake
  sudo apt-get install libtool
  sudo apt-get install zlib1g zlib1g-dev
fi

log "CONFIGURATION  = ${CONFIGURATION}"
log "BUILD SSL      = ${BUILD_SSL}"
log "BUILD CURL     = ${BUILD_CURL}"
log "CMakeLists.txt = ${SCRIPT_DIR}"
log "CMake output   = ${SCRIPT_DIR}/../../Int/CMake/libHttpClient.Linux"

# make libcrypto and libssl
if [ "$BUILD_SSL" = true ]; then
    log "Building SSL"
    sed -i -e 's/\r$//' "$SCRIPT_DIR"/openssl_Linux.bash
    bash "$SCRIPT_DIR"/openssl_Linux.bash -c "$CONFIGURATION"
fi

if [ "$BUILD_CURL" = true ]; then
    log "Building cURL"
    sed -i -e 's/\r$//' "$SCRIPT_DIR"/curl_Linux.bash
    bash "$SCRIPT_DIR"/curl_Linux.bash -c "$CONFIGURATION"
fi

if [ "$BUILD_STATIC" = false ]; then
    # make libHttpClient static
    sudo cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR"/../../Int/CMake/libHttpClient.Linux -D CMAKE_BUILD_TYPE=$CONFIGURATION -D BUILD_SHARED_LIBS=YES -D CMAKE_CXX_COMPILER=clang++-11 -D CMAKE_C_COMPILER=clang-11
    sudo make -C "$SCRIPT_DIR"/../../Int/CMake/libHttpClient.Linux
else
    # make libHttpClient shared
    sudo cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR"/../../Int/CMake/libHttpClient.Linux -D CMAKE_BUILD_TYPE=$CONFIGURATION -D CMAKE_CXX_COMPILER=clang++-11 -D CMAKE_C_COMPILER=clang-11
    sudo make -C "$SCRIPT_DIR"/../../Int/CMake/libHttpClient.Linux
fi