#!/bin/bash
set -e

USAGE="./scripts/pre-commit-test.sh"

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
TEST_SCRIPT=${ROOT_DIR}/scripts/test.sh
BUILD_SCRIPT=${ROOT_DIR}/scripts/build.sh
SETUP_SCRIPT=${ROOT_DIR}/scripts/setup.sh
RUST_BINDINGS_DIR=${ROOT_DIR}/sandook/virtual_disk/rust_bindings
LOADGEN_DIR=${ROOT_DIR}/loadgen

if [ -z "$NET_IFACE_NAME" ]
then
  echo "NET_IFACE_NAME not set"
  exit 1
else
  echo "NET_IFACE_NAME: ${NET_IFACE_NAME}"
fi

if [ -z "$DISK_PCI_ADDR" ]
then
    echo "DISK_PCI_ADDR not defined"
    exit 1
else
   echo "DISK_PCI_ADDR: ${DISK_PCI_ADDR}"
fi

# Clean build (NO LTO) for building the Rust bindings
if ! NO_LTO=1 $BUILD_SCRIPT clean 1> .build.log 2>&1
then
  echo "Build (NO LTO) failed! See .build.log for details."
   exit 1
fi
rm .build.log

# Build Rust bindings
pushd .
cd $RUST_BINDINGS_DIR
cargo clean 1> $ROOT_DIR/.build.log 2>&1
if ! cargo build --release 1> $ROOT_DIR/.build.log 2>&1
then
  echo "Build (Rust bindings) failed! See .build.log for details."
   exit 1
fi
popd
rm .build.log

# Build the loadgen
pushd .
cd $LOADGEN_DIR
cargo clean 1> $ROOT_DIR/.build.log 2>&1
if ! cargo build --release 1> $ROOT_DIR/.build.log 2>&1
then
  echo "Build (loadgen) failed! See .build.log for details."
   exit 1
fi
popd
rm .build.log

# Clean build
if ! $BUILD_SCRIPT clean 1> .build.log 2>&1
then
   echo "Build failed! See .build.log for details."
   exit 1
fi
rm .build.log

# Setup machine
if ! $SETUP_SCRIPT $NET_IFACE_NAME 1> .setup.log 2>&1
then
   echo "Setup failed! See .setup.log for details."
   exit 1
fi
rm .setup.log

# Run tests
TEST_MODE="minimal"
if ! $TEST_SCRIPT $NET_IFACE_NAME $DISK_PCI_ADDR $TEST_MODE 1> .test.log 2>&1
then
   echo "Test failed! See .test.log for details."
   exit 1
fi
rm .test.log
