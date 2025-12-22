#!/bin/bash
set -ex

USAGE="usage: $0 <net_interface_name>"

SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
BUILD_DIR=${ROOT_DIR}/build

source $SCRIPT_DIR/helper.sh

CONTROLLER_CONFIG=${SCRIPT_DIR}/../build/sandook/controller/controller.config
SANDOOK_CONFIG_PATH=${SCRIPT_DIR}/../build/config.json
DISK_SERVER_CONFIG=${SCRIPT_DIR}/../build/sandook/disk_server/disk_server_spdk.config

if [ "$#" -ne 1 ]
then
  echo "${USAGE}"
  exit 1
fi

BLOCK_SIZE_BYTES=4096
SET_RATES=(0 250 500 750 1000)
MPPS_LIST=(1.1 1.0 0.04 0.03 0.6)
SAMPLES=30
RUNTIME=3000
FAKE_ADDR="192.168.127.9:5050"

IF_NAME=$1
NET_PCI_ADDR=`sudo lshw -c network -businfo -quiet | grep $IF_NAME | awk '{print substr($1,5)}'`

TOTAL_CORES=`nproc`
SOCKETS=`lscpu | grep Socket | awk '{print $2}'`
CORES=$((TOTAL_CORES / SOCKETS))
CONTROLLER_CORES=2
AVAILABLE_CORES=$((CORES - CONTROLLER_CORES - 2))
MAX_LOADGEN_CORES=20
LOADGEN_CORES=$(( AVAILABLE_CORES < MAX_LOADGEN_CORES ? AVAILABLE_CORES : MAX_LOADGEN_CORES ))

function build {
  bash $SCRIPT_DIR/build.sh clean
}

function build_without_lto {
  export NO_LTO=1
  bash $SCRIPT_DIR/build.sh clean
}

function update_configs {
  DISK_PCI_ADDR=$1

  update_cores $CONTROLLER_CORES $CONTROLLER_CONFIG
  update_cores $LOADGEN_CORES $DISK_SERVER_CONFIG
  update_disk_pci_addr $DISK_PCI_ADDR $DISK_SERVER_CONFIG

  sed -i "s/192.168.127/192.168.147/g" $SANDOOK_CONFIG_PATH
  sed -i "s/192.168.127/192.168.147/g" $CONTROLLER_CONFIG
  sed -i "s/192.168.127/192.168.147/g" $DISK_SERVER_CONFIG

  sed -i "s/kVirtualDiskType\": .*\"/kVirtualDiskType\": \"Local\"/g" $SANDOOK_CONFIG_PATH
}

function run_loadgen {
  DISK_SERIAL_NUM=$1
  SET_RATE=$2
  MPPS=$3

  SUFFIX="100r"
  if [ $SET_RATE -ne 0 ];
  then
    SUFFIX="${SET_RATE}w"
  fi

  LOADGEN_DIR=${SCRIPT_DIR}/../loadgen
  pushd ${LOADGEN_DIR}

  export SANDOOK_CONFIG=$SANDOOK_CONFIG_PATH
  cargo run --release \
            --features sandook \
            --bin synthetic ${FAKE_ADDR} \
            --mode runtime-client \
            --config ${DISK_SERVER_CONFIG} \
            -p sandook \
            --transport fake \
            --threads $((LOADGEN_CORES - 1)) \
            --samples ${SAMPLES} \
            --mpps ${MPPS} \
            --runtime ${RUNTIME} \
            --rampup 0 \
            -d constant \
            --sandook_set_rate ${SET_RATE} | tee /tmp/loadgen.log
  cat /tmp/loadgen.log | grep -E 'Distribution|constant' | sed 's/ //g' | tee temp.log

  awk -v sn="${DISK_SERIAL_NUM}" 'BEGIN{FS=OFS=","} /Distribution/{$0="SerialNumber,"$0} !/Distribution/{$0=sn","$0} {print}' temp.log | tee -a ${DISK_SERIAL_NUM}_${SUFFIX}.csv

  rm -f /tmp/loadgen.log
  rm -f temp.log
  popd
}

# Get all SSD PCIe addresses
DISK_PCI_ADDRS_RAW=`sudo lshw -c storage -businfo -quiet | grep NVMe | awk '{print substr($1,5)}'`

# Get corresponding SSD serial numbers
DISK_SERIAL_NUMS=()
DISK_PCI_ADDRS=()
DEVICES=()

unbind_spdk
for DISK_PCI_ADDR in $DISK_PCI_ADDRS_RAW;
do
  DISK_SERIAL_NUM=`sudo nvme list -v | grep ${DISK_PCI_ADDR} | awk '{print $2}'`
  DEVICE=`sudo nvme list | grep ${DISK_SERIAL_NUM} | awk '{print $1}'`
  DISK_SERIAL_NUMS=(${DISK_SERIAL_NUMS[@]} $DISK_SERIAL_NUM)
  DEVICES=(${DEVICES[@]} $DEVICE)
  DISK_PCI_ADDRS=(${DISK_PCI_ADDRS[@]} $DISK_PCI_ADDR)
  echo "${DISK_PCI_ADDR} -> ${DISK_SERIAL_NUM}"
done

for DEVICE in $DEVICES;
do
  sudo nvme format $DEVICE --block-size=$BLOCK_SIZE_BYTES --force
  sudo blkdiscard $DEVICE
done
bind_spdk

# Build sandook without LTO to run loadgen
build_without_lto

stop_iokerneld
start_iokerneld $NET_PCI_ADDR

for i in "${!DISK_PCI_ADDRS[@]}";
do
  DISK_PCI_ADDR=${DISK_PCI_ADDRS[$i]}
  DISK_SERIAL_NUM=${DISK_SERIAL_NUMS[$i]}

  update_configs $DISK_PCI_ADDR

  for j in "${!SET_RATES[@]}";
  do
    SET_RATE=${SET_RATES[$j]}
    MPPS=${MPPS_LIST[$j]}

    echo $DISK_PCI_ADDR $DISK_SERIAL_NUM $SET_RATE $MPPS

    run_loadgen $DISK_SERIAL_NUM $SET_RATE $MPPS
    teardown_tests
  done
done

# Clean up because some configurations were modified.
rm -rf $BUILD_DIR
