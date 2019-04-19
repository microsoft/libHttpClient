#!/bin/bash
set | grep ARCH
set -x

OPENSSL_SRC="$SRCROOT/../../External/openssl"
OPENSSL_TMP="$OPENSSL_SRC/tmp"
LIB_OUTPUT="${SCRIPT_INPUT_FILE_0}"

if [ "$LIB_OUTPUT" == "" ]; then
echo "***** No library output directory specified - bailing out *****"
exit 1
fi

BUILDARCHS="$ARCHS"

# check whether libcrypto.a already exists for this architecture - we'll only build if it does not
if [ -f  "$LIB_OUTPUT/lib/libcrypto.a" ]; then

EXISTING_ARCHS="$(lipo -info $LIB_OUTPUT/lib/libcrypto.a)"
ARCH_MISSING=0

for BUILDARCH in $BUILDARCHS
do
if [[ $EXISTING_ARCHS != *"$BUILDARCH"* ]]; then
ARCH_MISSING=1
fi
done

if [ $ARCH_MISSING == 1 ]; then
echo "***** Rebuilding previously-built library to support new architectures *****"
else
echo "***** Using previously-built libary $LIB_OUTPUT/lib/libcrypto.a - skipping build *****"
exit 0;
fi

else

echo "***** No previously-built libary present at $LIB_OUTPUT/lib/libcrypto.a - performing build *****"

fi

# make dirs
mkdir -p "$OPENSSL_TMP"
mkdir -p "$LIB_OUTPUT/lib"
mkdir -p "$LIB_OUTPUT/include"

# figure out the right set of build architectures for this run
echo "***** creating universal binary for architectures: $BUILDARCHS *****"

if [ "$SDKROOT" != "" ]; then
ISYSROOT="-isysroot $SDKROOT"
fi

cd $OPENSSL_SRC
for BUILDARCH in $BUILDARCHS
do
echo "***** BUILDING UNIVERSAL ARCH $BUILDARCH ******"
make clean

if [[ "$BUILDARCH" = *"x86_64"* ]]; then
./Configure darwin64-x86_64-cc shared enable-ec_nistp_64_gcc_128 no-ssl2 no-ssl3 no-comp no-async --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
elif [[ "$BUILDARCH" = *"i386"* ]]; then
./Configure darwin-i386-cc shared no-ssl2 no-ssl3 no-comp no-async --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
elif [[ "$BUILDARCH" = *"arm64"* ]]; then

export CROSS_TOP=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer
export CROSS_SDK=iPhoneOS.sdk
export PATH="/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin:$PATH"
export BUILD_TOOLS="${DEVELOPER}"
export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${BUILDARCH}"

./Configure ios64-cross no-shared no-dso no-hw no-engine no-async -fembed-bitcode enable-ec_nistp_64_gcc_128 --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
else
export CROSS_TOP=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer
export CROSS_SDK=iPhoneOS.sdk
export PATH="/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin:$PATH"
export BUILD_TOOLS="${DEVELOPER}"
export CC="${BUILD_TOOLS}/usr/bin/gcc -arch ${BUILDARCH}"

./Configure ios-cross no-shared no-dso no-hw no-engine no-async -fembed-bitcode --prefix="$OPENSSL_TMP/" --openssldir="$OPENSSL_TMP/"
fi

# installs openssl for this flavor
make install

echo "***** renaming intermediate libraries to $CONFIGURATION_TEMP_DIR/$BUILDARCH-*.a *****"
cp "$OPENSSL_TMP"/lib/libcrypto.a "$CONFIGURATION_TEMP_DIR"/$BUILDARCH-libcrypto.a
cp "$OPENSSL_TMP"/lib/libssl.a "$CONFIGURATION_TEMP_DIR"/$BUILDARCH-libssl.a
done

# combines each flavor's library into one universal library

echo "***** creating universallibraries in $LIB_OUTPUT *****"
lipo -create "$CONFIGURATION_TEMP_DIR/"*-libcrypto.a -output "$LIB_OUTPUT/lib/libcrypto.a"
lipo -create "$CONFIGURATION_TEMP_DIR/"*-libssl.a -output "$LIB_OUTPUT/lib/libssl.a"
cp -r "$OPENSSL_TMP/include/"* "$LIB_OUTPUT/include/"

echo "***** cleaning artifacts *****"
rm -rf "$OPENSSL_TMP/"
rm -rf "$CONFIGURATION_TEMP_DIR/"

echo "***** executing ranlib on libraries in $TARGET_BUILD_DIR *****"
ranlib "$LIB_OUTPUT/lib/libcrypto.a"
ranlib "$LIB_OUTPUT/lib/libssl.a"
