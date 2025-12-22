#!/bin/bash
set -xe

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
PATCHES_DIR=${ROOT_DIR}/lib/patches
CALADAN_PATCHES_DIR=${ROOT_DIR}/lib/patches/caladan
CALADAN_DIR=${ROOT_DIR}/lib/caladan

# Install Linux packages
sudo -E apt install -y make cmake pkg-config libnl-3-dev libnl-route-3-dev libnuma-dev uuid-dev libssl-dev libaio-dev libcunit1-dev libclang-dev libncurses-dev meson python3-pyelftools

# Patch build config
cd $CALADAN_PATCHES_DIR
patch -p1 -N -d $CALADAN_DIR < shared_mk.patch
patch -p1 -N -d $CALADAN_DIR < config.patch
patch -p1 -N -d $CALADAN_DIR < memless.patch
patch -p1 -N -d $CALADAN_DIR < tcache.patch
patch -p1 -N -d $CALADAN_DIR < spdk_ns.patch
patch -p1 -N -d $CALADAN_DIR < storage.patch
patch -p1 -N -d $CALADAN_DIR < log.patch
patch -p1 -N -d $CALADAN_DIR < ssd_serial_num.patch
patch -p1 -N -d $CALADAN_DIR < storage-bindings-for-TRIM.patch

# Install Caladan
cd $CALADAN_DIR
make submodules
(cd ksched && make -j `nproc`)

# Patch the SPDK setup script
cd $CALADAN_PATCHES_DIR
cp spdk_setup.sh $CALADAN_DIR/spdk/scripts/setup.sh
