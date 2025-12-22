#!/bin/bash
set -xe

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
FLAMEGRAPH_DIR=${ROOT_DIR}/lib/FlameGraph

OUTPUT_DIR_ROOT="perf_results"
mkdir -p ${OUTPUT_DIR_ROOT}

OUTPUT_DIR="${OUTPUT_DIR_ROOT}/perf_$(date '+%Y-%m-%d_%H-%M-%S')"
mkdir ${OUTPUT_DIR}

# Record
sudo perf record -F 10000 --buildid-mmap --call-graph dwarf -o ${OUTPUT_DIR}/perf.data ${1+"$@"}

# Generate flamegraph
sudo perf script -i ${OUTPUT_DIR}/perf.data > ${OUTPUT_DIR}/perf.script
sudo perl ${FLAMEGRAPH_DIR}/stackcollapse-perf.pl ${OUTPUT_DIR}/perf.script > ${OUTPUT_DIR}/flamegraph.folded
sudo perl ${FLAMEGRAPH_DIR}/flamegraph.pl ${OUTPUT_DIR}/flamegraph.folded > ${OUTPUT_DIR}/flamegraph.svg

# Set permissions
sudo chmod 777 ${OUTPUT_DIR}

echo "Output stored in: ${OUTPUT_DIR}"
