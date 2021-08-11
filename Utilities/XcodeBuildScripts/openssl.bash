#!/bin/bash

set | grep ARCH
set -x

log () {
    echo "***** $1 *****"
}

env

exit 0

### Set up environment variables ###

BUILD_ARCHS="$ARCHS"

DEVELOPER_DIR="$(xcode-select -p)"
export CROSS_COMPILE="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/"

if [ "$PLATFORM_NAME" == "macosx" ]; then
    PLAT="MacOSX"
elif [ "$PLATFORM_NAME" == "iphoneos" ]; then
    PLAT="iPhoneOS"
elif [ "$PLATFORM_NAME" == "iphonesimulator" ]; then
    PLAT="iPhoneSimulator"
else
    log "Unexpected or missing PLATFORM_NAME: $PLATFORM_NAME - bailing out"
    exit 1
fi

export CROSS_TOP="$DEVELOPER_DIR/Platforms/$PLAT.platform/Developer"
export CROSS_SDK="$PLAT.sdk"

log "Preparing build for architectures $BUILD_ARCHS on platform $PLATFORM_NAME"

### Set up build locations ###

if [ "$OPENSSL_TMP_DIR" == "" ]; then
    log "No tmp build directory provided - bailing out"
    exit 1
fi

if [ "$OPENSSL_LIB_OUTPUT" == "" ]; then
    log "No library output directory provided - bailing out"
    exit 1
fi

OPENSSL_SRC="$SRCROOT/../../External/openssl"
OPENSSL_TMP="$OPENSSL_TMP_DIR"
LIB_OUTPUT="$OPENSSL_LIB_OUTPUT/$PLATFORM_NAME"

### Check whether libcrypto.a already exists for this architecture/platform - we'll only build if it does not ###

if [ -f "$LIB_OUTPUT/lib/libcrypto.a" ]; then
    EXISTING_ARCHS="$(lipo -info $LIB_OUTPUT/lib/libcrypto.a)"

    ARCH_MISSING=0
    for BUILD_ARCH in $BUILD_ARCHS; do
        if [[ $EXISTING_ARCHS != *"$BUILD_ARCH"* ]]; then
            ARCH_MISSING=1
        fi
    done

    if [ $ARCH_MISSING == 1 ]; then
        log "Rebuilding previously-built library, architectures missing"
    else
        log "Previously-built library present at $LIB_OUTPUT/lib/libcrypto.a - skipping build"
        exit 0
    fi

else
    log "No previously-built library present at $LIB_OUTPUT/lib/libcrypto.a - performing build"
fi

### Set up build dirs ###

mkdir -p "$OPENSSL_TMP"
mkdir -p "$LIB_OUTPUT/lib"
mkdir -p "$LIB_OUTPUT/include"

pushd $OPENSSL_SRC

### Configure and build for each architecture ###

for BUILD_ARCH in $BUILD_ARCHS; do
    log "Cleaning..."

    make clean

    log "Configuring for architecture $BUILD_ARCH and platform $PLATFORM_NAME"

    export CC="$DEVELOPER_DIR/usr/bin/gcc -arch $BUILD_ARCH"

    if [ "$BUILD_ARCH" == "x86_64" ]; then
        ./Configure darwin64-x86_64-cc shared enable-ec_nistp_64_gcc_128 no-ssl2 no-ssl3 no-comp no-async --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
    elif [ "$BUILD_ARCH" == "arm64" ]; then
        if [ "$PLATFORM_NAME" == "macosx" ] || [ "$PLATFORM_NAME" == "iphonesimulator" ]; then
            ./Configure darwin64-arm64-cc shared enable-ec_nistp_64_gcc_128 no-ssl2 no-ssl3 no-comp no-async --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
        elif [ "$PLATFORM_NAME" == "iphoneos" ]; then
            ./Configure ios64-cross no-shared no-dso no-hw no-engine no-async -fembed-bitcode enable-ec_nistp_64_gcc_128 --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
        fi
    else
        log "Unexpected architecture: $BUILD_ARCH"
        exit 1
    fi

    # Build OpenSSL (just the software components, no docs/manpages)
    make install_sw

    log "Renaming intermediate libraries to $CONFIGURATION_TEMP_DIR/$BUILD_ARCH-*.a"
    cp "$OPENSSL_TMP"/lib/libcrypto.a "$CONFIGURATION_TEMP_DIR"/$BUILD_ARCH-libcrypto.a
    cp "$OPENSSL_TMP"/lib/libssl.a "$CONFIGURATION_TEMP_DIR"/$BUILD_ARCH-libssl.a
done

# Combine all the architectures into one universal library

log "Creating universal libraries in $LIB_OUTPUT"
lipo -create "$CONFIGURATION_TEMP_DIR/"*-libcrypto.a -output "$LIB_OUTPUT/lib/libcrypto.a"
lipo -create "$CONFIGURATION_TEMP_DIR/"*-libssl.a -output "$LIB_OUTPUT/lib/libssl.a"

log "Copying headers to $LIB_OUTPUT"
cp -r "$OPENSSL_TMP/include/*" "$LIB_OUTPUT/include/"

log "Cleaning artifacts"
rm -rf "$OPENSSL_TMP"
rm -rf "$CONFIGURATION_TEMP_DIR"

log "Executing ranlib on universal libraries in $LIB_OUTPUT"
ranlib "$LIB_OUTPUT/lib/libcrypto.a"
ranlib "$LIB_OUTPUT/lib/libssl.a"

log "OpenSSL build complete!"
