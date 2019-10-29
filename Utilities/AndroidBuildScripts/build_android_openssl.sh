# 2019-10-25
# This is a command-line script for building for Android using the CMake file directly
# Some variables have to be passed to this script as arguments or else defined
# as environment variables.

# Note that this builds libHttpClient assuming that OpenSSL and Crypto have been built first
# Outputs of this script are put in libHttpClient/Binaries/{Debug, Release}/{ABI}
# e.g. Binaries/Debug/x86_64

function usage() {
  echo ""
  echo "build_android_openssl"
  echo ""
  echo "This is a command-line script for building the openssl stack for Android using"
  echo "command-line NDK.  Some variables have to be passed to this script as arguments or"
  echo "else defined as environment variables."
  echo ""
  echo "Note that this builds libHttpClient assuming that OpenSSL and Crypto have been"
  echo "built first. Outputs of this script are put in the path"
  echo "libHttpClient/External/openssl.  these include the following:"
  echo "- libcrypto.a"
  echo "- libcrypto.so"
  echo "- libssl.a"
  echo "- libssl.so"
\  echo ""
  echo "Required arguments:"
  echo "  --android-sdk <path>"
  echo "    the path to the Android SDK to use.  this is required if ANDROID_SDK is not set."
  echo "  --android-ndk <path>"
  echo "    the path to the Android NDK to use.  this is required if ANDROID_NDK is not set."
  echo ""
  echo "Optional arguments:"
  echo "  --noclean"
  echo "    do not clean the build first"
  echo "  --dry-run"
  echo "    do not actually do the important commands, just show what would happen"
  echo "  --android-abi <abi>"
  echo "    the single abi to build for (x86, x86_64, armeabi-v7a, arm64-v8a). defaults to x86_64"
  echo "  --android-abis \"<abi-1> <abi-2> ...\""
  echo "    a list of ABIs in string quotes to build multiple ABIs (overrides --android-abi) "
  echo "  --android-native-api-level <number>"
  echo "    the native API level to use.  defaults to 21."
  echo "  --android-toolchain [clang | gcc]"
  echo "    the build tool to use.  defaults to clang."
  echo "  --build-type [Debug | Release]"
  echo "    the type of build variant (Debug or Release).  defaults to Release."
  echo "  --all-build-types"
  echo "    builds all build variants (both Debug and Release)"
  echo ""
}

function arch_name() {
    abi=$3
    arch=
    if [[ -z "$abi" ]]; then
      return 1
    fi
    if [[ "$abi" == "x86_64" ]]; then
      arch=android-x86_64
    elif [[ "$abi" == "x86" ]]; then
      arch=android-x86
    elif [[ "$abi" == "armeabi-v7a" ]]; then
      arch=android-arm
    elif [[ "$abi" == "arm64-v8a" ]]; then
      arch=android-arm64
    else
      return 1
    fi
    echo $arch
    return 0
}

function destination_dir() {
    projectRoot=$1
    buildType=$2
    abi=$3
    abiDirname=
    buildDirname=
    if [[ -z "$projectRoot" || -z "$abi" || -z "$buildType" ]]; then
      return 1
    fi
    binariesDir=${projectRoot}/Binaries
    if [[ "$abi" == "x86_64" ]]; then
      abiDirname=x64
    elif [[ "$abi" == "x86" ]]; then
      abiDirname=x86
    elif [[ "$abi" == "armeabi-v7a" ]]; then
      abiDirname=ARM
    elif [[ "$abi" == "arm64-v8a" ]]; then
      abiDirname=ARM64
    else
      return 1
    fi
    if [[ "$buildType" == "Debug" || "$buildType" == "debug" ]]; then
      buildDirname=Debug
    elif [[ "$buildType" == "Release" || "$buildType" == "release" ]]; then
      buildDirname=Release
    else
      return 1
    fi
    destdir=${binariesDir}/${buildDirname}/${abiDirname}
    echo $destdir
    return 0
}

function copy_build_output() {
    srcpath=$1
    binariesDir=$2
    archpath=$3
    if [[ -z "$archpath" || -z "$binariesDir" || -z "$srcpath" ]]; then
      echo "unable to copy build outputs, src or destination path are empty"
      return 1
    fi
    if [ ! -d ${binariesDir}/include ]; then
      mkdir -p ${binariesDir}/include
    fi
}


# setup paths
workingdir=`pwd`
cmdline=$0
basepath=${cmdline%/*}
if [[ "$basepath" == "." ]]; then
  basepath=
fi
fullpath=${workingdir}/${basepath}
libHttpClientRoot=${fullpath}../..
opensslRelPath=External/openssl
opensslDir="${libHttpClientRoot}/${opensslRelPath}"
os_arch_name=darwin-x86_64    # we could calculate this if we had to with uname -a
toolchainsRelPath=toolchains/llvm/prebuilt/${os_arch_name}
toolchainsPath=

# iterate through arguments and process options
androidSdk=
androidNdk=

androidAbiList=
makeBuildTypeList=

androidAbi=x86_64
androidNativeApiLevel=21
androidToolchain=clang
makeBuildType=Release
noClean=
quiet=0
dryrun=0

# how to get this dynamically?  sucks that this has to be hardcoded
outputFile=

# find path to CMake file

while [ -n "$1" ]; do
  arg=$1
  shift
  if [[ "$arg" == "--android-sdk" ]]; then
    androidSdk=$1
    shift
  elif [[ "$arg" == "--android-ndk" ]]; then
    androidNdk=$1
    shift
  elif [[ "$arg" == "--android-abi" ]]; then
    androidAbi=$1
    shift
  elif [[ "$arg" == "--android-abi-list" ]]; then
    androidAbiList=$1
    shift
  elif [[ "$arg" == "--all-abis" ]]; then
    androidAbiList="x86 x86_64 armeabi-v7a arm64-v8a"
  elif [[ "$arg" == "--android-native-api-level" ]]; then
    androidNativeApiLevel=$1
    shift
  elif [[ "$arg" == "--android-toolchain" ]]; then
    androidToolchain=$1
    shift
  elif [[ "$arg" == "--build-type" ]]; then
    makeBuildType=$1
    shift
  elif [[ "$arg" == "--all-build-types" ]]; then
    cmakeBuildTypeList="Debug Release"
  elif [[ "$arg" == "--noclean" ]]; then
    noclean=1
  elif [[ "$arg" == "--dry-run" ]]; then
    dryrun=1
  elif [[ "$arg" == "--quiet" || "$arg" == "-q" ]]; then
    quiet=1
  elif [[ "$arg" == "--help" || "$arg" == "-?" ]]; then
    usage
    shift
    exit
  fi

done

# set the variables to environment variables if they weren't passed as command args

# set the SDK dir
if [ -z "$androidSdk" ]; then
  if [ ! -z "$ANDROID_SDK" ]; then
    androidSdk=$ANDROID_SDK
  elif [ ! -z "$ANDROID_SDK_HOME" ]; then
    androidSdk=$ANDROID_SDK_HOME
  fi
fi

# set the NDK dir
if [ -z "$androidNdk" ]; then
  if [ ! -z "$ANDROID_NDK" ]; then
    androidNdk=$ANDROID_NDK
  elif [ ! -z "$ANDROID_NDK_HOME" ]; then
    androidNdk=$ANDROID_NDK_HOME
  fi
fi

# after that, if SDK or NDK aren't set, we have to error out
if [[ -z "$androidSdk" || -z "$androidNdk" ]]; then
  echo "Android SDK and NDK paths must be provided. Either pass arguments"
  echo "--android-sdk and --android-ndk or set the environment variables"
  echo "ANDROID_SDK_HOME and ANDROID_NDK_HOME"
  exit 1
fi

export ANDROID_NDK_HOME=$androidNdk
export ANDROID_SDK_HOME=$androidSdk

CC=$androidToolchain

toolchainsPath=${androidNdk}/${toolchainsRelPath}
PATH=${toolchainsPath}/bin:$PATH
ANDROID_API=${androidNativeApiLevel}


# create target directories
for build in Debug Release; do
  for abi in x86 x86_64 armeabi-v7a arm64-v8a; do
    destpath=`destination_dir $opensslDir $build $abi`
    result=$?
    if [[ "$result" == "0" ]]; then
      [[ "$quiet" == "0" ]] && echo "creating target path $destpath"
      [[ "$dryrun" == "0" ]] && mkdir -p ${destpath}
    fi
  done
done

# if there is no list of build types, make the single one a list
if [ -z "$makeBuildTypeList" ]; then
  makeBuildTypeList=$cmakeBuildType
fi
# convert to array
makeBuildTypeList=($makeBuildTypeList)

# if they didn't provide a list, make the single Abi a list
if [ -z "$androidAbiList" ]; then
  androidAbiList=$androidAbi
fi
# convert to array
androidAbiList=($androidAbiList)

# go to directory and build
cd $opensslDir

for buildType in ${makeBuildTypeList[@]}; do
  [[ "$quiet" == "0" ]] && echo ""
  [[ "$quiet" == "0" ]] && echo "building for ${buildType} build variant --"
  for abi in ${androidAbiList[@]}; do
    [[ "$quiet" == "0" ]] && echo ""
    [[ "$quiet" == "0" ]] && echo "building ${buildType} for $abi ABI --"

    architecture=`arch_name $abi`
    result=$?
    if [[ "$result" != "0" ]]; then
      echo "cannot determine android architecture from ABI"
      exit 1
    fi

    commandline="./Configure ${architecture} -D__ANDROID_API__=$ANDROID_API"
    [[ "$quiet" == "0" ]] && echo "command line ="
    [[ "$quiet" == "0" ]] && echo $commandline
    [[ "$quiet" == "0" ]] && echo "running Configure"
    if [[ "$dryrun" == "0" ]]; then
      $commandline
      result=$?
      if [[ "$result" != "0" ]]; then
        echo "the 'Configure' command returned an error"
        exit $result
      fi
    fi
    if [ -z "$noclean" ]; then
      [[ "$quiet" == "0" ]] && echo "cleaning build..."
      if [[ "$dryrun" == "0" ]]; then
        make clean
        result=$?
        if [[ "$result" != "0" ]]; then
          echo "the 'make clean' command returned an error"
          exit $result
        fi
      fi
    fi
    [[ "$quiet" == "0" ]] && echo "building..."
    if [[ "$dryrun" == "0" ]]; then
      make
      result=$?
      if [[ "$result" != "0" ]]; then
        echo "the 'make' command returned an error"
        exit $result
      fi
    fi
    # copy output file to destination dir
    if [ -f ${outputFile} ]; then
      destpath=`destination_dir $libHttpClientRoot $buildType $abi`
      result=$?
      if [[ "$result" == "0" ]]; then
        copy_outputs ${destpath}
        cp ${outputFile} ${destpath}
        if [[ "$?" != "0" ]]; then
          echo "unable to copy project output to destination ${destpath}!"
          exit 1
        fi
      fi
    else
      echo "project target output file ${outputFile} not found!"
      exit 1
    fi
  done
done
exit 0
