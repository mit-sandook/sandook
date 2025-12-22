#!/bin/bash

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
BUILD_DIR=${ROOT_DIR}/build
CALADAN_DIR=${ROOT_DIR}/lib/caladan
SPDK_DIR=${CALADAN_DIR}/spdk
UBDSRV_DIR=${ROOT_DIR}/lib/ubdsrv
TEST_DIR=${BUILD_DIR}/sandook
TEST_OUTPUT_DIR=/tmp/sandook_test_output
TRACES_OUTPUT_DIR=/dev/shm/sandook
BIN_DIR=${ROOT_DIR}/bin
CTEST=${BIN_DIR}/bin/ctest
SANDOOK_CONFIG=${BUILD_DIR}/config.json
SPDK_SETUP_SCRIPT=${SPDK_DIR}/scripts/setup.sh

SPDK_BACKEND="SPDK"

RETURN=0

function assert_success {
  if [[ $? -ne 0 ]]
  then
    echo -e "----\e[32mFAILED\e[0m"
    exit 1
  fi
}

function update_disk_pci_addr {
  disk_pci_addr=$1
  caladan_config=$2

  sed -i "s/attach_spdk_pci.*/attach_spdk_pci $disk_pci_addr/g" $caladan_config
}

function update_disk_server_backend {
  backend=$1
  sandook_config=$2

  sed -i "s/kDiskServerBackend\": .*\"/kDiskServerBackend\": \"$backend\"/g" $sandook_config
}

function update_disk_server_ip {
  ip=$1
  caladan_config=$2
  sandook_config=$3

  sed -i "s/host_addr [0-9]*\.[0-9]*\.[0-9]*\.[0-9]*/host_addr $ip/g" $caladan_config
  sed -i "s/\"kStorageServerIP\": \"[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*\"/\"kStorageServerIP\": \"$ip\"/g" $sandook_config
}

function update_controller_self_ip {
  ip=$1
  caladan_config=$2

  sed -i "s/host_addr [0-9]*\.[0-9]*\.[0-9]*\.[0-9]*/host_addr $ip/g" $caladan_config
}

function update_controller_ip {
  ip=$1
  sandook_config=$2

  sed -i "s/\"kControllerIP\": \"[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*\"/\"kControllerIP\": \"$ip\"/g" $sandook_config
}

function update_cores {
  cores=$1
  filepath=$2

  sed -i "s/runtime_kthreads [0-9]*/runtime_kthreads $cores/g" $filepath
  sed -i "s/runtime_spinning_kthreads [0-9]*/runtime_spinning_kthreads $cores/g" $filepath
  sed -i "s/runtime_guaranteed_kthreads [0-9]*/runtime_guaranteed_kthreads $cores/g" $filepath
}

function update_controller_control_plane_scheduler_type {
  sched_type=$1
  sandook_config=$2

  sed -i "s/kControlPlaneSchedulerType\": .*\"/kControlPlaneSchedulerType\": \"$sched_type\"/g" $sandook_config
}

function update_controller_data_plane_scheduler_type {
  sched_type=$1
  sandook_config=$2

  sed -i "s/kDataPlaneSchedulerType\": .*\"/kDataPlaneSchedulerType\": \"$sched_type\"/g" $sandook_config
}

function update_controller_disk_server_rejections {
  disk_server_rejections=$1
  sandook_config=$2

  sed -i "s/kDiskServerRejections\": .*/kDiskServerRejections\": $disk_server_rejections,/g" $sandook_config
}

function update_blk_dev_ip {
  ip=$1
  sandook_config=$2

  sed -i "s/\"kVirtualDiskIP\": \"[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*\"/\"kVirtualDiskIP\": \"$ip\"/g" $sandook_config
}

function bind_spdk {
  sudo $SPDK_SETUP_SCRIPT
  sleep 5
}

function unbind_spdk {
  sudo $SPDK_SETUP_SCRIPT reset
  sleep 5
}

function stop_iokerneld {
  sudo kill -KILL $(pidof iokerneld) &> /dev/null || true
}

function start_iokerneld {
  NET_PCI_ADDR=$1
  OPTIONAL_PARAMS=$2

  if pidof -qx "iokerneld"; then
    # iokerneld is already running
    return
  fi

  stop_controller
  stop_disk_server

  IOKERNELD_LOG=/tmp/iokerneld.log
  sudo rm -rf $IOKERNELD_LOG

  cd $CALADAN_DIR
  (sudo pkill iokerneld && sleep 2) || true
  echo "Launching iokerneld..."
  sudo nohup ./iokerneld ias nicpci $NET_PCI_ADDR $OPTIONAL_PARAMS > $IOKERNELD_LOG 2>&1 &
  assert_success

  set +x
  while ! grep -q 'running dataplane' $IOKERNELD_LOG; do
    sleep 0.3
    pgrep iokerneld > /dev/null
  done
  set -x

  sleep 1
  reset
  echo "Launched iokerneld!"
}

function start_controller {
  stop_controller

  sudo rm -rf $TRACES_OUTPUT_DIR

  controller_log=/tmp/controller.log
  sudo rm -rf $controller_log

  controller_dir=$BUILD_DIR/sandook/controller
  controller=$controller_dir/controller
  controller_config=$controller_dir/controller.config

  cd $controller_dir
  SANDOOK_CONFIG="$SANDOOK_CONFIG" sudo -E stdbuf -oL -eL nohup $controller $controller_config > $controller_log 2>&1 &

  sleep 5
  reset
}

function start_controller_with_params {
  output_dir=$1
  controller_ip=$2
  control_plane_sched_type=$3
  data_plane_sched_type=$4
  disk_server_rejections=$5

  stop_controller

  sudo rm -rf $TRACES_OUTPUT_DIR

  timestamp=$(date -d "today" +"%Y%m%d%H%M%S%3N")

  controller_log=${output_dir}/controller_${timestamp}.log
  sudo rm -rf $controller_log

  controller_dir=$BUILD_DIR/sandook/controller
  controller=$controller_dir/controller
  controller_caladan_config_template=$controller_dir/controller.config
  controller_caladan_config=${output_dir}/controller_${timestamp}.config
  cp $controller_caladan_config_template $controller_caladan_config

  controller_sandook_config_template=$BUILD_DIR/config.json
  controller_sandook_config=${output_dir}/controller_${timestamp}.json
  cp $controller_sandook_config_template $controller_sandook_config

  update_controller_self_ip $controller_ip $controller_caladan_config 
  update_controller_ip $controller_ip $controller_sandook_config 
  update_controller_control_plane_scheduler_type $control_plane_sched_type $controller_sandook_config
  update_controller_data_plane_scheduler_type $data_plane_sched_type $controller_sandook_config
  update_controller_disk_server_rejections $disk_server_rejections $controller_sandook_config

  cd $controller_dir
  SANDOOK_CONFIG="$controller_sandook_config" sudo -E stdbuf -oL -eL nohup $controller $controller_caladan_config > $controller_log 2>&1 &

  sleep 5
  reset
}

function stop_controller {
  sudo kill -s SIGTERM $(pidof controller) &> /dev/null || true
  sudo kill -KILL $(pidof controller) &> /dev/null || true
}

function start_disk_server {
  disk_pci_addr=$1

  timestamp=$(date -d "today" +"%Y%m%d%H%M%S%3N")

  disk_server_log=/tmp/disk_server.log
  sudo rm -rf $disk_server_log

  disk_server_dir=$BUILD_DIR/sandook/disk_server
  disk_server=$disk_server_dir/disk_server
  disk_server_config=$disk_server_dir/disk_server_spdk.config

  disk_server_sandook_config_template=$BUILD_DIR/config.json
  disk_server_sandook_config=/tmp/disk_server_${timestamp}.json
  cp $disk_server_sandook_config_template $disk_server_sandook_config

  update_disk_pci_addr $disk_pci_addr $disk_server_config 
  update_disk_server_backend $SPDK_BACKEND $disk_server_sandook_config

  cd $disk_server_dir
  SANDOOK_CONFIG="$SANDOOK_CONFIG" sudo -E nohup $disk_server $disk_server_config > $disk_server_log 2>&1 &

  sleep 10
  reset
}

function start_disk_server_with_params {
  output_dir=$1
  disk_pci_addr=$2
  disk_server_ip=$3
  controller_ip=$4

  timestamp=$(date -d "today" +"%Y%m%d%H%M%S%3N")

  disk_server_log=${output_dir}/disk_server_${timestamp}.log
  sudo rm -rf $disk_server_log

  disk_server_dir=$BUILD_DIR/sandook/disk_server
  disk_server=$disk_server_dir/disk_server
  disk_server_caladan_config_template=$disk_server_dir/disk_server_spdk.config
  disk_server_caladan_config=${output_dir}/disk_server_${timestamp}.config
  cp $disk_server_caladan_config_template $disk_server_caladan_config

  disk_server_sandook_config_template=$BUILD_DIR/config.json
  disk_server_sandook_config=${output_dir}/disk_server_${timestamp}.json
  cp $disk_server_sandook_config_template $disk_server_sandook_config

  update_disk_pci_addr $disk_pci_addr $disk_server_caladan_config 
  update_disk_server_ip $disk_server_ip $disk_server_caladan_config $disk_server_sandook_config
  update_controller_ip $controller_ip $disk_server_sandook_config
  update_disk_server_backend $SPDK_BACKEND $disk_server_sandook_config

  cd $disk_server_dir
  SANDOOK_CONFIG="$disk_server_sandook_config" sudo -E stdbuf -oL -eL nohup $disk_server $disk_server_caladan_config > $disk_server_log 2>&1 &

  sleep 10
  reset
}

function stop_disk_server {
  sudo kill -s SIGTERM $(pidof disk_server) &> /dev/null || true
  sudo kill -KILL $(pidof disk_server) &> /dev/null || true
}

function start_storage_perf {
  output_dir=$1
  disk_pci_addr=$2

  timestamp=$(date -d "today" +"%Y%m%d%H%M%S%3N")

  storage_perf_log=${output_dir}/storage_perf_${timestamp}.log
  sudo rm -rf $storage_perf_log

  storage_perf_dir=$BUILD_DIR/sandook/utils
  storage_perf=$storage_perf_dir/storage_perf
  storage_perf_caladan_config_template=$storage_perf_dir/storage_perf.config
  storage_perf_caladan_config=${output_dir}/storage_perf_${timestamp}.config
  cp $storage_perf_caladan_config_template $storage_perf_caladan_config

  storage_perf_sandook_config_template=$BUILD_DIR/config.json
  storage_perf_sandook_config=${output_dir}/storage_perf_${timestamp}.json
  cp $storage_perf_sandook_config_template $storage_perf_sandook_config

  update_disk_pci_addr $disk_pci_addr $storage_perf_caladan_config 
  update_disk_server_backend $SPDK_BACKEND $storage_perf_sandook_config

  cd $storage_perf_dir
  SANDOOK_CONFIG="$storage_perf_sandook_config" sudo -E stdbuf -oL -eL nohup $storage_perf $storage_perf_caladan_config 2>&1 | tee $storage_perf_log 

  sleep 5
  reset
}

function start_disk_pre_fill {
  output_dir=$1
  disk_pci_addr=$2

  timestamp=$(date -d "today" +"%Y%m%d%H%M%S%3N")

  disk_pre_fill_log=${output_dir}/disk_pre_fill_${timestamp}.log
  sudo rm -rf $disk_pre_fill_log

  disk_pre_fill_dir=$BUILD_DIR/sandook/utils
  disk_pre_fill=$disk_pre_fill_dir/pre_fill
  disk_pre_fill_caladan_config_template=$disk_pre_fill_dir/pre_fill.config
  disk_pre_fill_caladan_config=${output_dir}/disk_pre_fill_${timestamp}.config
  cp $disk_pre_fill_caladan_config_template $disk_pre_fill_caladan_config

  disk_pre_fill_sandook_config_template=$BUILD_DIR/config.json
  disk_pre_fill_sandook_config=${output_dir}/disk_pre_fill_${timestamp}.json
  cp $disk_pre_fill_sandook_config_template $disk_pre_fill_sandook_config

  update_disk_pci_addr $disk_pci_addr $disk_pre_fill_caladan_config 
  update_disk_server_backend $SPDK_BACKEND $disk_pre_fill_sandook_config

  cd $disk_pre_fill_dir
  SANDOOK_CONFIG="$disk_pre_fill_sandook_config" sudo -E stdbuf -oL -eL nohup $disk_pre_fill $disk_pre_fill_caladan_config 2>&1 | tee $disk_pre_fill_log 

  sleep 5
  reset
}

function start_blk_dev {
  output_dir=$1
  BLK_DEV_IP=$2
  AFFINITY_LIST=$3
  echo "Starting blk_dev on following cores: $AFFINITY_LIST"
  #stop_blk_dev

  timestamp=$(date -d "today" +"%Y%m%d%H%M%S%3N")

  blk_dev_log=${output_dir}/blk_dev_${timestamp}.log
  sudo rm -rf $blk_dev_log

  BLK_DEV_DIR=$BUILD_DIR/sandook/blk_dev
  BLK_DEV=$BLK_DEV_DIR/blk_dev

  # Create a config file for blk_dev
  blk_dev_caladan_config_template=$BLK_DEV_DIR/blk_dev.config
  blk_dev_caladan_config=${output_dir}/blk_dev_${timestamp}.config
  cp $blk_dev_caladan_config_template $blk_dev_caladan_config

  blk_dev_sandook_config_template=$BUILD_DIR/config.json
  blk_dev_sandook_config=${output_dir}/disk_server_${timestamp}.json
  cp $blk_dev_sandook_config_template $blk_dev_sandook_config

  update_controller_self_ip $BLK_DEV_IP $blk_dev_caladan_config
  update_blk_dev_ip $BLK_DEV_IP $blk_dev_sandook_config

  cd $BLK_DEV_DIR
  SANDOOK_CONFIG="$blk_dev_sandook_config" sudo -E stdbuf -oL -eL nohup $BLK_DEV $blk_dev_caladan_config $AFFINITY_LIST > $blk_dev_log 2>&1 &
  sleep 5
  reset
}

function stop_blk_dev {
  sudo kill -KILL $(pidof blk_dev) &> /dev/null || true

  ublk=${UBDSRV_DIR}/ublk || true
  #sudo $ublk del -a || true
}

function setup_tests {
  disk_pci_addr=$1

  sudo rm -rf $TRACES_OUTPUT_DIR
  sudo rm -rf $TEST_OUTPUT_DIR
  mkdir $TEST_OUTPUT_DIR

  start_controller
  start_disk_server $disk_pci_addr
}

function run_tests {
  sleep 2

  cd $TEST_DIR
  export GTEST_COLOR=1
  if ! $CTEST --output-on-failure --verbose --timeout 300 --tests-regex "^test_"
  then
    RETURN=1
  fi
}

function run_block_device_tests {
  sleep 2

  cd $TEST_DIR
  export GTEST_COLOR=1
  if ! $CTEST --output-on-failure --verbose --timeout 300 --tests-regex "^test_blk_dev" --gtest_also_run_disabled_tests
  then
    RETURN=1
  fi
}

function run_benchmarks {
  sleep 2

  cd $TEST_DIR
  export GTEST_COLOR=1
  if ! $CTEST --output-on-failure --verbose --timeout 300 --tests-regex "^bench_"
  then
    RETURN=1
  fi
}

function teardown_tests {
  sudo rm -rf $TEST_OUTPUT_DIR

  stop_blk_dev
  stop_disk_server
  stop_controller
}
