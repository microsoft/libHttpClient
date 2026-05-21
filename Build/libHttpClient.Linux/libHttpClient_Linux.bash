#!/bin/bash

set -e

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
BUILD_UNREAL_ENGINE_4=false
ENABLE_WEBSOCKET_COMPRESSION=true
C_COMPILER="clang"
CXX_COMPILER="clang++"
INSTALL_DEPENDENCIES=false
REQUIRE_VERIFIED_DEPENDENCIES=true
BUILD_DIR="$SCRIPT_DIR"/../../Int/CMake/libHttpClient.Linux

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
    -ue4|--unreal-engine-4)
      BUILD_UNREAL_ENGINE_4=true
      shift
      ;;
    --install-dependencies)
      INSTALL_DEPENDENCIES=true
      shift
      ;;
    -wc|--websocket-compression)
      ENABLE_WEBSOCKET_COMPRESSION=true
      shift
      ;;
    -nwc|--no-websocket-compression)
      ENABLE_WEBSOCKET_COMPRESSION=false
      shift
      ;;
    -sg|--skipaptget)
      # NOOP. allow user to specify old --skipaptget args before that became the default
      shift
      ;;
    -sd|--skip-dependency-check)
      REQUIRE_VERIFIED_DEPENDENCIES=false
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

set +e # temporarily disable exit-on-error to provide more graceful handling of dependency installation failures
if [ "$INSTALL_DEPENDENCIES" = "true" ]; then
    bash "$SCRIPT_DIR"/install_dependencies.bash
    if [ $? -ne 0 ]; then
      echo ""
      echo "Failed to install dependencies."
      exit 1
    fi
else
    bash "$SCRIPT_DIR"/install_dependencies.bash --check
    if [ $? -ne 0 ]; then
        if [ "$REQUIRE_VERIFIED_DEPENDENCIES" = true ]; then
            echo ""
            echo "Some dependencies are missing."
            echo "Please run with --install-dependencies to install them or run $SCRIPT_DIR/install_dependencies.bash directly"
            exit 1
        else
            echo ""
            echo "Some dependencies are missing."
            echo "--skip-dependency-check specified, ignoring and continuing."
            echo ""
        fi
    fi
fi
set -e # re-enable exit-on-error after dependency installation check

log "CONFIGURATION  = ${CONFIGURATION}"
log "BUILD SSL      = ${BUILD_SSL}"
log "BUILD CURL     = ${BUILD_CURL}"
log "WS COMPRESSION = ${ENABLE_WEBSOCKET_COMPRESSION}"
log "CMakeLists.txt = ${SCRIPT_DIR}"
log "CMake output   = ${BUILD_DIR}"

if [ "$BUILD_UNREAL_ENGINE_4" = true ]; then
    log "Unreal Compatibility Enabled"
    C_COMPILER="clang-11"
    CXX_COMPILER="clang++-11"
else
    log "Unreal Compatibility Disabled"
fi

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

WEBSOCKET_COMPRESSION_CMAKE_ARG="-DHC_ENABLE_WEBSOCKET_COMPRESSION=ON"
if [ "$ENABLE_WEBSOCKET_COMPRESSION" != true ]; then
  WEBSOCKET_COMPRESSION_CMAKE_ARG="-DHC_ENABLE_WEBSOCKET_COMPRESSION=OFF"
fi

if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
  CACHED_BUILD_DIR=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | head -n 1)
  CACHED_SOURCE_DIR=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | head -n 1)
  if [ "$CACHED_BUILD_DIR" != "$BUILD_DIR" ] || [ "$CACHED_SOURCE_DIR" != "$SCRIPT_DIR" ]; then
    log "Removing stale CMake cache from ${BUILD_DIR}"
    rm -rf "$BUILD_DIR"
  fi
fi

MAKE_PARALLELISM="-j$(nproc)" # run Make in parallel to speed up the build process
if [ "$BUILD_STATIC" = false ]; then
    # make libHttpClient shared
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -D CMAKE_BUILD_TYPE=$CONFIGURATION -D CMAKE_C_COMPILER=$C_COMPILER -D CMAKE_CXX_COMPILER=$CXX_COMPILER -D BUILD_SHARED_LIBS=ON $WEBSOCKET_COMPRESSION_CMAKE_ARG
    make $MAKE_PARALLELISM -C "$BUILD_DIR"
else
    # make libHttpClient static
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -D CMAKE_BUILD_TYPE=$CONFIGURATION -D CMAKE_C_COMPILER=$C_COMPILER -D CMAKE_CXX_COMPILER=$CXX_COMPILER -D BUILD_SHARED_LIBS=OFF $WEBSOCKET_COMPRESSION_CMAKE_ARG
    make $MAKE_PARALLELISM -C "$BUILD_DIR"
fi
