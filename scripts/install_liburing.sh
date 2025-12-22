#!/bin/bash
set -xe

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
LIB_DIR=${ROOT_DIR}/lib
LIBURING_DIR=${LIB_DIR}/liburing

cd $LIBURING_DIR

# Build liburing from source
./configure --cc=gcc --cxx=g++;
make -j
sudo make install
