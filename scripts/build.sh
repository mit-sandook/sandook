#!/bin/bash
set -xe

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
BUILD_DIR=${ROOT_DIR}/build
BIN_DIR=${ROOT_DIR}/bin
CMAKE=${BIN_DIR}/bin/cmake

# CMake parameters
BUILD_TYPE=Release

for var in "$@"
do
  if [[ $var == "debug" ]]; then
    BUILD_TYPE=Debug
  elif [[ $var == "clean" ]]; then
    sudo rm -rf $BUILD_DIR
  else
    echo "Unknown command line option: ${var}"
    exit 1
  fi
done

# Create build directory
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# Build project
$CMAKE -DDISABLE_LTO=$NO_LTO -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
make -j `nproc`
