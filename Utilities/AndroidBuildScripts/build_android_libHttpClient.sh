# 2019-10-25
# This is a command-line script for building for Android using the CMake file directly
# Some variables have to be passed to this script as arguments or else defined
# as environment variables.

# Note that this builds libHttpClient assuming that OpenSSL and Crypto have been built first
# Outputs of this script are put in libHttpClient/output/libs/{Debug, Release}/{ABI}
# e.g. libs/Debug/x86_64

function usage() {
  echo ""
  echo "build_android"
  echo ""
  echo "This is a command-line script for building for Android using the CMake file"
  echo "directly.  Some variables have to be passed to this script as arguments or"
  echo "else defined as environment variables."
  echo ""
  echo "Note that this builds libHttpClient assuming that OpenSSL and Crypto have been"
  echo "built first. Outputs of this script are put in the path"
  echo "  libHttpClient/output/libs/ {Debug, Release}/{ABI}"
  echo "e.g. libs/Debug/x86_64"
  echo ""
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
  echo "  --cmake-file <path>"
  echo "    the path to the CMakeLists.txt file to use.  defaults to ../CMake"
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
  echo "  --cmake-exec <path>"
  echo "    the path to the CMake executable.  if not provided, it attempts to find this in the Android SDK."
  echo "  --cmake-toolchain-file <path>"
  echo "    the path to the CMake toolchain file.  if not provided, it attempts to find this in the NDK."
  echo ""
}

function destination_arch_dir() {
  # this returns the proper libs directory for a given build type and arch
  projectRoot=$1
  buildType=$2
  abi=$3
  abiDirname=
  buildDirname=
  if [[ -z "$projectRoot" || -z "$abi" || -z "$buildType" ]]; then
    return 1
  fi
  libsDir=${projectRoot}/output/libs
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
  destdir=${libsDir}/${buildDirname}/${abiDirname}
  echo $destdir
  return 0
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
libsDir=${libHttpClientRoot}/output

# iterate through arguments and process options
androidSdk=
androidNdk=
cmakeExec=
cmakeToolchainFile=
cmakeRoot=
cmakeVersion=
cmakeToolchainRelPath=build/cmake/android.toolchain.cmake
androidAbiList=
cmakeBuildTypeList=

androidAbi=x86_64
androidNativeApiLevel=21
androidToolchain=clang
cmakeBuildType=Release
cmakeFile=
cmakeFileDir=
noClean=
quiet=0
dryrun=0

# how to get this dynamically?  sucks that this has to be hardcoded
outputFile=liblibHttpClient.Android.C.a

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
  elif [[ "$arg" == "--cmake-file" ]]; then
    cmakeFile=$1
    shift
  elif [[ "$arg" == "--cmake-exec" ]]; then
    cmakeExec=$1
    shift
  elif [[ "$arg" == "--cmake-toolchain-file" ]]; then
    cmakeToolchainFile=$1
    shift
  elif [[ "$arg" == "--build-type" ]]; then
    cmakeBuildType=$1
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

# find the CMake file if it wasn't specified
parentDir=${fullpath%/}
parentDir=${parentDir%/*}
cmakeFileDir=${parentDir}/CMake
if [ -d "${cmakeFileDir}" ]; then
  # the directory exists, check if CMake file is there
  if [ -f "${cmakeFileDir}/CMakeLists.txt" ]; then
    cmakeFile=${cmakeFileDir}/CMakeFiles.txt
    [[ "$quiet" == "0" ]] && echo "using CMakeFiles.txt found at $cmakeFile"
  fi
fi


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
  echo "ANDROID_SDK and ANDROID_NDK"
  exit 1
fi

if [ -z "$cmakeExec" ]; then
  # see if we can find CMake
  cmakeRoot=
  cmakeVersion=
  cmakeExec=
  if [ -n "$androidSdk" ]; then
    if [ -d "$androidSdk/cmake" ]; then
      cmakeVersion=`ls $androidSdk/cmake`
      cmakeRoot=${androidSdk}/cmake/${cmakeVersion}
      cmakeExec=${cmakeRoot}/bin/cmake
      [[ "$quiet" == "0" ]] && echo "using CMake executable found at $cmakeExec"
    fi
  fi
fi

# set the cmake toolchaing file
if [ -z "$cmakeToolchainFile" ]; then
  if [ -n "$androidNdk" ]; then
    if [ -f "${androidNdk}/${cmakeToolchainRelPath}" ]; then
      cmakeToolchainFile=${androidNdk}/${cmakeToolchainRelPath}
      [[ "$quiet" == "0" ]] && echo "using Cmake toolchain file found at $cmakeToolchainFile"
    fi
  fi
fi

# create target directories
for build in Debug Release; do
  for abi in x86 x86_64 armeabi-v7a arm64-v8a; do
    destArchDir=`destination_arch_dir $libHttpClientRoot $build $abi`
    result=$?
    if [[ "$result" == "0" ]]; then
      [[ "$quiet" == "0" ]] && echo "creating target path $destArchDir"
      [[ "$dryrun" == "0" ]] && mkdir -p ${destArchDir}
    fi
  done
done

# example command-line for the CMakeLists.txt in ../CMake:
# $cmakeExec -DBUILDANDROID=ON -DCMAKE_TOOLCHAIN_FILE=/Users/jjclose/Library/Android/sdk/ndk/20.0.5594570/build/cmake/android.toolchain.cmake  \
#   -DANDROID_ABI=x86_64 \
#   -DANDROID_NATIVE_API_LEVEL=21 \
#   -DANDROID_TOOLCHAIN=clang \
#   -DANDROID_NDK=${ANDROID_NDK} \
#   -DCMAKE_BUILD_TYPE=Release

# if there is no list of build types, make the single one a list
if [ -z "$cmakeBuildTypeList" ]; then
  cmakeBuildTypeList=$cmakeBuildType
fi
# convert to array
cmakeBuildTypeList=($cmakeBuildTypeList)

# if they didn't provide a list, make the single Abi a list
if [ -z "$androidAbiList" ]; then
  androidAbiList=$androidAbi
fi
# convert to array
androidAbiList=($androidAbiList)

# go to directory and build
cd $cmakeFileDir
for buildType in ${cmakeBuildTypeList[@]}; do
  [[ "$quiet" == "0" ]] && echo ""
  [[ "$quiet" == "0" ]] && echo "building for ${buildType} build variant --"
  for abi in ${androidAbiList[@]}; do
    [[ "$quiet" == "0" ]] && echo ""
    [[ "$quiet" == "0" ]] && echo "building ${buildType} for $abi ABI --"
    commandline="$cmakeExec -DBUILDANDROID=ON -DCMAKE_TOOLCHAIN_FILE=${cmakeToolchainFile}  \
      -DANDROID_ABI=${abi} \
      -DANDROID_NATIVE_API_LEVEL=${androidNativeApiLevel} \
      -DANDROID_TOOLCHAIN=${cmakeToolchain} \
      -DANDROID_NDK=${androidNdk} \
      -DCMAKE_BUILD_TYPE=${buildType}"
    [[ "$quiet" == "0" ]] && echo "command line ="
    [[ "$quiet" == "0" ]] && echo $commandline
    [[ "$quiet" == "0" ]] && echo "running cmake"
    if [[ "$dryrun" == "0" ]]; then
      $commandline
      result=$?
      if [[ "$result" != "0" ]]; then
        echo "the 'cmake' command returned an error"
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
      destArchDir=`destination_arch_dir $libHttpClientRoot $buildType $abi`
      result=$?
      if [[ "$result" == "0" ]]; then
        cp ${outputFile} ${destArchDir}
        if [[ "$?" != "0" ]]; then
          echo "unable to copy project output to destination ${destArchDir}!"
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
