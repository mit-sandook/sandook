#!/bin/bash
set -xe

USAGE="./scripts/reset.sh"

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
CALADAN_DIR=${ROOT_DIR}/lib/caladan

if [ "$#" -ne 0 ]
then
  echo "usage: ${USAGE}"
  exit 1
fi

function reset_spdk {
  pushd $CALADAN_DIR/spdk
  sudo ./scripts/setup.sh reset
  popd
}

reset_spdk
