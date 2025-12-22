#!/bin/bash
set -ex

USAGE="usage: $0 <[sample|rpc_bench|iokerneld|controller|disk_server|teardown]>"
USAGE_SAMPLE="usage: $0 sample <net_interface_name>"
USAGE_RPC_BENCH="usage: $0 rpc_bench <net_interface_name>"
USAGE_IOKERNELD="usage: $0 iokerneld <net_interface_name>"
USAGE_CONTROLLER="usage: $0 controller <net_interface_name> <output_dir> <controller_ip> <control_plane_scheduler_type> <data_plane_scheduler_type> <is_disk_server_rejections_enabled [0|1]>"
USAGE_DISK_SERVER="usage: $0 disk_server <net_interface_name> <output_dir> <disk_pci_address> <disk_server_ip> <controller_ip>"
USAGE_STORAGE_PERF="usage: $0 storage_perf <net_interface_name> <output_dir> <disk_pci_address>"
USAGE_DISK_PRE_FILL="usage: $0 disk_pre_fill <net_interface_name> <output_dir> <disk_pci_address>"
USAGE_TEARDOWN="usage: $0 teardown"
USAGE_BLK_DEV="usage: $0 blk_dev <blk_dev_ip> <affinity_list>"

SCRIPT_DIR=$(dirname $(readlink -f $0))
source $SCRIPT_DIR/helper.sh

function start_rpcbench_server {
  stop_rpcbench

  RPCBENCH_DIR=$BUILD_DIR/sandook/test
  RPCBENCH=$RPCBENCH_DIR/bench_rpc
  RPCBENCH_CONFIG=$RPCBENCH_DIR/bench_rpc_server.config

  sudo $RPCBENCH $RPCBENCH_CONFIG server 2>&1 &
  sleep 1
  reset
}

function start_rpcbench_client {
  RPCBENCH_DIR=$BUILD_DIR/sandook/test
  RPCBENCH=$RPCBENCH_DIR/bench_rpc
  RPCBENCH_CONFIG=$RPCBENCH_DIR/bench_rpc_client.config
  SERVER_CONFIG=$RPCBENCH_DIR/bench_rpc_server.config
  server_ip=$(cat $SERVER_CONFIG | grep host_addr | awk '{print $2}')
  cores=1

  sudo $RPCBENCH $RPCBENCH_CONFIG client $server_ip $cores 10000 128
}

function stop_rpcbench {
  sudo kill -KILL $(pidof bench_rpc) &> /dev/null || true
}

function run_read_write_program {
  SAMPLE_DIR=$BUILD_DIR/sandook/samples/read_write_blk_dev
  SAMPLE=$SAMPLE_DIR/read_write_blk_dev
  sudo $SAMPLE --device /dev/ublkb0 --length 100
  sudo $SAMPLE --device /dev/ublkb0 --write --payload testingtestingtesting

  sudo kill -KILL $(pidof read_write_blk_dev) &> /dev/null || true
}

function run_read_write_test {
  net_pci_addr=$1

  start_iokerneld $net_pci_addr
  setup_tests

  run_read_write_program

  teardown_tests
  stop_iokerneld

  exit $ret
}

function run_rpc_bench {
  net_pci_addr=$1

  if [ ! -f $BUILD_DIR/"sandook/test/bench_rpc" ]; then
    bash $SCRIPT_DIR/build.sh clean
  fi

  start_iokerneld $net_pci_addr
  start_rpcbench_server

  start_rpcbench_client

  stop_rpcbench
  stop_iokerneld

  exit $ret
}

function run {
    if [ "$1" == "teardown" ]
    then
        teardown_tests
        exit 0
    fi

    if [ "$#" -gt 1 ]
    then
      if_name=$2
      net_pci_addr=`sudo lshw -c network -businfo -quiet | grep $if_name | awk '{print substr($1,5)}'`
    fi

    if [ "$1" == "sample" ]
    then
      if [ "$#" -lt 2 ]
      then
        echo ${USAGE_SAMPLE}
        exit 1
      fi

      run_read_write_test $net_pci_addr
    elif [ "$1" == "rpc_bench" ]
    then
      if [ "$#" -lt 2 ]
      then
        echo ${USAGE_RPC_BENCH}
        exit 1
      fi

      run_rpc_bench $net_pci_addr
    elif [ "$1" == "iokerneld" ]
    then
      if [ "$#" -lt 1 ]
      then
        echo ${USAGE_IOKERNELD}
        exit 1
      fi

      optional_params=$3
      start_iokerneld $net_pci_addr $optional_params
    elif [ "$1" == "controller" ]
    then
      if [ "$#" -lt 6 ]
      then
        echo ${USAGE_CONTROLLER}
        exit 1
      fi

      output_dir=$3
      controller_ip=$4
      control_plane_sched_type=$5
      data_plane_sched_type=$6
      disk_server_rejections=$7
      start_iokerneld $net_pci_addr
      start_controller_with_params $output_dir $controller_ip $control_plane_sched_type $data_plane_sched_type $disk_server_rejections
    elif [ "$1" == "disk_server" ]
    then
      if [ "$#" -lt 5 ]
      then
        echo ${USAGE_DISK_SERVER}
        exit 1
      fi

      output_dir=$3
      disk_pci_addr=$4
      disk_server_ip=$5
      controller_ip=$6
      start_iokerneld $net_pci_addr
      start_disk_server_with_params $output_dir $disk_pci_addr $disk_server_ip $controller_ip
    elif [ "$1" == "storage_perf" ]
    then
      if [ "$#" -lt 3 ]
      then
        echo ${USAGE_STORAGE_PERF}
        exit 1
      fi

      output_dir=$3
      disk_pci_addr=$4
      start_iokerneld $net_pci_addr
      start_storage_perf $output_dir $disk_pci_addr
    elif [ "$1" == "disk_pre_fill" ]
    then
      if [ "$#" -lt 3 ]
      then
        echo ${USAGE_disk_pre_fill}
        exit 1
      fi

      output_dir=$3
      disk_pci_addr=$4
      start_iokerneld $net_pci_addr
      start_disk_pre_fill $output_dir $disk_pci_addr
    elif [ "$1" == "blk_dev" ]
    then
      if [ "$#" -lt 4 ]
      then
        echo ${USAGE_BLK_DEV}
        exit 1
      fi

      output_dir=$2
      blk_dev_ip=$3
      affinity_list=$4
      start_blk_dev $output_dir $blk_dev_ip $affinity_list
    fi

}

if [ "$#" -lt 1 ]
then
  echo ${USAGE}
  exit 1
fi

run $1 ${@:2}
