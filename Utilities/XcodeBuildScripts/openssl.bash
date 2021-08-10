#!/bin/bash

set | grep ARCH
set -x

log () {
    echo "***** $1 *****"
}

OPENSSL_SRC="$SRCROOT/../../External/openssl"
OPENSSL_TMP="$OPENSSL_TMP_DIR"
LIB_OUTPUT="$OPENSSL_LIB_OUTPUT"

if [ "$OPENSSL_TMP" == "" ]; then
    log "No tmp build directory specified - bailing out"
    exit 1
fi

if [ "$LIB_OUTPUT" == "" ]; then
    log "No library output directory specified - bailing out"
    exit 1
fi

BUILDARCHS="$ARCHS"

# check whether libcrypto.a already exists for this architecture - we'll only build if it does not
if [ -f "$LIB_OUTPUT/lib/libcrypto.a" ]; then

    EXISTING_ARCHS="$(lipo -info $LIB_OUTPUT/lib/libcrypto.a)"
    ARCH_MISSING=0

    for BUILDARCH in $BUILDARCHS; do
        if [[ $EXISTING_ARCHS != *"$BUILDARCH"* ]]; then
            ARCH_MISSING=1
        fi
    done

    if [ $ARCH_MISSING == 1 ]; then
        log "Rebuilding previously-built library to support new architectures"
    else
        log "Using previously-built libary $LIB_OUTPUT/lib/libcrypto.a - skipping build"
        exit 0
    fi

else
    log "No previously-built libary present at $LIB_OUTPUT/lib/libcrypto.a - performing build"
fi

# make dirs
mkdir -p "$OPENSSL_TMP"
mkdir -p "$LIB_OUTPUT/lib"
mkdir -p "$LIB_OUTPUT/include"

# figure out the right set of build architectures for this run
log "creating universal binary for architectures: $BUILDARCHS"

if [ "$SDKROOT" != "" ]; then
    ISYSROOT="-isysroot $SDKROOT"
fi

cd $OPENSSL_SRC
for BUILDARCH in $BUILDARCHS; do
    echo "***** BUILDING UNIVERSAL ARCH $BUILDARCH ******"
    make clean

    if [[ "$BUILDARCH" = *"x86_64"* ]]; then
        ./Configure darwin64-x86_64-cc shared enable-ec_nistp_64_gcc_128 no-ssl2 no-ssl3 no-comp no-async --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
    elif [[ "$BUILDARCH" = *"i386"* ]]; then
        ./Configure darwin-i386-cc shared no-ssl2 no-ssl3 no-comp no-async --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
    elif [[ "$BUILDARCH" = *"arm64"* ]]; then

        export CROSS_TOP="$(xcode-select -p)/Platforms/iPhoneOS.platform/Developer"
        export CROSS_SDK=iPhoneOS.sdk
        export PATH="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin:$PATH"
        export BUILD_TOOLS="${DEVELOPER}"
        export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${BUILDARCH}"

        ./Configure ios64-cross no-shared no-dso no-hw no-engine no-async -fembed-bitcode enable-ec_nistp_64_gcc_128 --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
    else
        export CROSS_TOP="$(xcode-select -p)/Platforms/iPhoneOS.platform/Developer"
        export CROSS_SDK=iPhoneOS.sdk
        export PATH="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin:$PATH"
        export BUILD_TOOLS="${DEVELOPER}"
        export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${BUILDARCH}"

        ./Configure ios-cross no-shared no-dso no-hw no-engine no-async -fembed-bitcode --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
    fi

    # installs openssl (just the software components, no docs/manpages) for this flavor
    make install_sw

    log "renaming intermediate libraries to $CONFIGURATION_TEMP_DIR/$BUILDARCH-*.a"
    cp "$OPENSSL_TMP"/lib/libcrypto.a "$CONFIGURATION_TEMP_DIR"/$BUILDARCH-libcrypto.a
    cp "$OPENSSL_TMP"/lib/libssl.a "$CONFIGURATION_TEMP_DIR"/$BUILDARCH-libssl.a
done

# combines each flavor's library into one universal library

log "creating universallibraries in $LIB_OUTPUT"
lipo -create "$CONFIGURATION_TEMP_DIR/"*-libcrypto.a -output "$LIB_OUTPUT/lib/libcrypto.a"
lipo -create "$CONFIGURATION_TEMP_DIR/"*-libssl.a -output "$LIB_OUTPUT/lib/libssl.a"
cp -r "$OPENSSL_TMP/include/"* "$LIB_OUTPUT/include/"

log "cleaning artifacts"
rm -rf "$OPENSSL_TMP/"
rm -rf "$CONFIGURATION_TEMP_DIR/"

log "executing ranlib on libraries in $TARGET_BUILD_DIR"
ranlib "$LIB_OUTPUT/lib/libcrypto.a"
ranlib "$LIB_OUTPUT/lib/libssl.a"
