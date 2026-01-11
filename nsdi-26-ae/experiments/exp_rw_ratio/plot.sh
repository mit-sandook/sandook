#!/bin/bash
set -ex

USAGE="Usage: ./plot.sh <output_dir>"
# <output_dir> is the directory containing the output files
# <output_dir>/<config_name>/loadgen_buckets/loadgen_merged.csv is the output file for <config_name>

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../

CONFIGS=("100w" "300w" "500w")

PY_VENV=".sandook-nsdi-26-ae"

function activate_py() {
  pushd .
  cd $ROOT_DIR
  . $PY_VENV/bin/activate
  popd
}

function plot() {
  output_dir=$1
  config_name=$2

  baseline_path=${output_dir}/${config_name}_static_rt/loadgen_buckets/loadgen_merged.csv
  if [ ! -f ${baseline_path} ]; then
    echo "Error: Baseline file not found: ${baseline_path}"
    return 1
  fi

  sandook_path=${output_dir}/${config_name}_sandook/loadgen_buckets/loadgen_merged.csv
  if [ ! -f ${sandook_path} ]; then
    echo "Error: Sandook file not found: ${sandook_path}"
    return 1
  fi

  # Output file
  output_filename=${output_dir}/fig-${config_name}

  activate_py
  python plot.py --baseline-traces ${baseline_path} \
                 --sandook-traces ${sandook_path} \
                 --output-filename ${output_filename} \
                 --show-legend 1
}

if [ $# -ne 1 ]; then
  echo $USAGE
  exit 1
fi

output_dir=$1
for config in "${CONFIGS[@]}"
do
  plot $output_dir $config || true
done
