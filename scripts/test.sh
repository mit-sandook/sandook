#!/bin/bash
set -e

USAGE="usage: $0 <net_iface_name> <disk_pci_addr> <[all|minimal|blk_dev|benchmarks]>"

SCRIPT_DIR=$(dirname $(readlink -f $0))
source $SCRIPT_DIR/helper.sh

function run {
  net_iface_name=$1
  disk_pci_addr=$2
  mode=$3

  net_pci_addr=`sudo lshw -c network -businfo -quiet | grep $net_iface_name | awk '{print substr($1,5)}'`

  start_iokerneld $net_pci_addr
  setup_tests $disk_pci_addr

  if [ "$mode" == "minimal" ]
  then
    run_tests
  elif [ "$mode" == "blk_dev" ]
  then
    run_block_device_tests
  elif [ "$mode" == "all" ]
  then
    run_tests
    run_block_device_tests
  elif [ "$mode" == "benchmarks" ]
  then
    run_benchmarks
  fi

  teardown_tests
  stop_iokerneld
}

if [ "$#" -lt 3 ]
then
  echo ${USAGE}
  exit 1
fi

run $1 ${@:2}

exit $RETURN
