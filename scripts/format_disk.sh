#!/bin/bash
set -ex

USAGE="usage: $0 <disk_pci_address>"

SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
BUILD_DIR=${ROOT_DIR}/build

source $SCRIPT_DIR/helper.sh

if [ "$#" -ne 1 ]
then
  echo "${USAGE}"
  exit 1
fi

DISK_PCI_ADDR=$1

BLOCK_SIZE_BYTES=4096

unbind_spdk

DISK_SERIAL_NUM=`sudo nvme list -v | grep ${DISK_PCI_ADDR} | awk '{print $2}'`
DEVICE=`sudo nvme list | grep ${DISK_SERIAL_NUM} | awk '{print $1}'`
sudo nvme format $DEVICE --block-size=$BLOCK_SIZE_BYTES --force
sudo blkdiscard $DEVICE

bind_spdk
