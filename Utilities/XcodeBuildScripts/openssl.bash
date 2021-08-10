#!/bin/bash

set | grep ARCH
set -x

log () {
    echo "***** $1 *****"
}

# Set up and validate input args

OPENSSL_SRC="$SRCROOT/../../External/openssl"
OPENSSL_TMP="$OPENSSL_TMP_DIR"
LIB_OUTPUT="$OPENSSL_LIB_OUTPUT"

if [ "$TARGET_PLATFORM" != "macOS" ] && [ "$TARGET_PLATFORM" != "iOS" ]; then
    log "Missing or invalid target platform: $TARGET_PLATFORM"
    exit 1
fi

if [ "$OPENSSL_TMP" == "" ]; then
    log "No tmp build directory specified - bailing out"
    exit 1
fi

if [ "$LIB_OUTPUT" == "" ]; then
    log "No library output directory specified - bailing out"
    exit 1
fi

BUILD_ARCHS="$ARCHS"

log "Requested architectures: $BUILD_ARCHS"

# Check whether libcrypto.a already exists for this architecture - we'll only build if it does not
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

# Set up build dirs
mkdir -p "$OPENSSL_TMP"
mkdir -p "$LIB_OUTPUT/lib"
mkdir -p "$LIB_OUTPUT/include"

pushd $OPENSSL_SRC

for BUILD_ARCH in $BUILD_ARCHS; do
    log "Clean-building for $BUILD_ARCH, targeting $TARGET_PLATFORM"

    make clean

    if [[ "$BUILD_ARCH" == *"x86_64"* ]]; then
        # Configure for x86_64 macOS build

        ./Configure darwin64-x86_64-cc shared enable-ec_nistp_64_gcc_128 no-ssl2 no-ssl3 no-comp no-async --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
    elif [[ "$BUILD_ARCH" == "*i386*" ]]; then
        # Configure for x86 macOS build

        ./Configure darwin-i386-cc shared no-ssl2 no-ssl3 no-comp no-async --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
    elif [[ "$BUILD_ARCH" == *"arm64"* ]]; then
        # Configure for arm64 iOS build

        export CROSS_TOP="$(xcode-select -p)/Platforms/iPhoneOS.platform/Developer"
        export CROSS_SDK=iPhoneOS.sdk
        export PATH="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin:$PATH"
        export CC="/usr/bin/clang -arch ${BUILD_ARCH}"

        ./Configure ios64-cross no-shared no-dso no-hw no-engine no-async -fembed-bitcode enable-ec_nistp_64_gcc_128 --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
    else
        # Configure for arm iOS build

        export CROSS_TOP="$(xcode-select -p)/Platforms/iPhoneOS.platform/Developer"
        export CROSS_SDK=iPhoneOS.sdk
        export CC="/usr/bin/clang -arch ${BUILD_ARCH}"

        ./Configure ios-cross no-shared no-dso no-hw no-engine no-async -fembed-bitcode --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
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
