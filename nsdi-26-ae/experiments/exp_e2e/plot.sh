#!/bin/bash
set -ex

USAGE="Usage: ./plot_techniques.sh <output_dir>"
# <output_dir> is the directory containing the output files
# <output_dir>/<config_name>/loadgen_buckets/loadgen_merged.csv is the output file for <config_name>

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../

PY_VENV=".sandook-nsdi-26-ae"

function activate_py() {
  pushd .
  cd $ROOT_DIR
  . $PY_VENV/bin/activate
  popd
}

function plot() {
  output_dir=$1

  # Baseline file
  baseline_path=${output_dir}/static_rt/loadgen_buckets/loadgen_merged.csv
  if [ ! -f ${baseline_path} ]; then
    echo "Error: Baseline file not found: ${baseline_path}"
    exit 1
  fi

  # RW file
  rw_path=${output_dir}/rw_seg/loadgen_buckets/loadgen_merged.csv
  if [ ! -f ${rw_path} ]; then
    echo "Error: RW file not found: ${rw_path}"
    exit 1
  fi

  # Sandook file
  sandook_path=${output_dir}/sandook/loadgen_buckets/loadgen_merged.csv
  if [ ! -f ${sandook_path} ]; then
    echo "Error: Sandook file not found: ${sandook_path}"
    exit 1
  fi

  # Output file
  output_filename=${output_dir}/fig-e2e

  activate_py
  python plot.py --baseline ${baseline_path} \
                 --rw ${rw_path} \
                 --sandook ${sandook_path} \
                 --output-filename ${output_filename}
}

if [ $# -ne 1 ]; then
  echo $USAGE
  exit 1
fi

output_dir=$1
plot $output_dir
