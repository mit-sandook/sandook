#!/bin/bash
set -xe

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
UBLK_PATCHES_DIR=${ROOT_DIR}/lib/patches/ublk
LIB_DIR=${ROOT_DIR}/lib
UBDSRV_DIR=${LIB_DIR}/ubdsrv
LIBURING_DIR=${LIB_DIR}/liburing

sudo apt-get install -y autoconf

cd $UBDSRV_DIR
patch -p1 -N < $UBLK_PATCHES_DIR/polling.patch || true

# Build ubdsrv from source
autoreconf -i
PKG_CONFIG_PATH=${LIBURING_DIR} \
./configure \
 CFLAGS="-I${LIBURING_DIR}/src/include -g -fPIC" \
 CXXFLAGS="-I${LIBURING_DIR}/src/include -g -fPIC" \
 LDFLAGS="-L${LIBURING_DIR}/src -g"
make clean
make -j
