#!/bin/bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

DO_INSTALL=true
while [[ $# -gt 0 ]]; do
  case $1 in
    --check)
      DO_INSTALL=false
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

build_dependencies=(clang make autoconf automake libtool)
library_dependencies=(zlib1g zlib1g-dev)

if [ "$DO_INSTALL" = false ]; then

  dependencies_missing=false
  for dep in "${build_dependencies[@]}" "${library_dependencies[@]}"; do
    if ! dpkg -s "$dep" &> /dev/null; then
      echo "Missing dependency: $dep"
      dependencies_missing=true
    else
      echo "Dependency installed: $dep"
    fi
  done

  if [ "$dependencies_missing" = true ]; then
    exit 1
  fi

  echo "All dependencies are installed"
  exit 0
fi

echo "Installing dependencies..."
echo "Build dependencies: ${build_dependencies[*]}"
echo "Library dependencies: ${library_dependencies[*]}"

sudo hwclock --hctosys
sudo apt-get update
sudo apt-get install "${build_dependencies[@]}" "${library_dependencies[@]}"