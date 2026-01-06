#!/bin/bash
set -xe

USAGE="./scripts/setup.sh <net_interface_name>"

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
CALADAN_DIR=${ROOT_DIR}/lib/caladan

if [ "$#" -ne 1 ]
then
  echo "usage: ${USAGE}"
  exit 1
fi

NET_IFACE_NAME=$1

function setup_ublk {
  sudo modprobe ublk_drv
}

function setup_caladan {
  pushd $CALADAN_DIR
  sudo ./scripts/setup_machine.sh
  popd
}

function setup_spdk {
  pushd $CALADAN_DIR/spdk
  sudo ./scripts/setup.sh
  popd
}

function setup_net_interface {
  sudo ifconfig $NET_IFACE_NAME mtu 9000 up
  ifconfig
}

function setup_trust_dscp {
    sudo mlnx_qos -i $NET_IFACE_NAME --trust dscp
}

function setup_pfc {
    sudo ethtool -A $NET_IFACE_NAME rx off tx off
    sudo mlnx_qos -i $NET_IFACE_NAME -f 1,1,1,1,1,1,1,1 \
                  -p 0,1,2,3,4,5,6,7 \
                  --prio2buffer 0,0,0,0,0,0,0,0 \
                  -s strict,strict,strict,strict,strict,strict,strict,strict
    sudo mlnx_qos -i $NET_IFACE_NAME --buffer_size=524160,0,0,0,0,0,0,0
}

setup_ublk || true
setup_caladan || true
setup_spdk || true
setup_net_interface || true
setup_trust_dscp || true
setup_pfc || true
