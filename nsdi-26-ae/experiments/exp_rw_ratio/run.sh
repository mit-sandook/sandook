#!/bin/bash
set -ex

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../

# Configurations to run
# CONFIGS=("read_heavy_sandook")
CONFIGS=("read_heavy_sandook" "read_heavy_static_rt")

# Python virtual environment
PY_VENV=".sandook-nsdi-26-ae"

# Base output directory
OUTPUT_DIR=${ROOT_DIR}/output
mkdir -p ${OUTPUT_DIR}

# Current output directory
timestamp=$(date +%Y%m%d-%H%M%S)
CURRENT_OUTPUT_DIR=${OUTPUT_DIR}/${timestamp}
mkdir -p ${CURRENT_OUTPUT_DIR}

# SSH key path
HOME_DIR=$(eval echo "~${USER}")
SSH_KEY_PATH=${HOME_DIR}/.ssh/id_rsa

function activate_py() {
  pushd .
  cd $ROOT_DIR
  . $PY_VENV/bin/activate
  popd
}

function run_exp() {
  config=$1

  # Config path
  config_path=${SCRIPT_DIR}/${config}.toml

  # Output directory for the current experiment
  output_dir=${CURRENT_OUTPUT_DIR}/${config}
  mkdir -p ${output_dir}

  pushd .
  cd $ROOT_DIR

  activate_py
  python -m exp_rw_ratio.run --config ${config_path} \
                             --user ${USER} \
                             --ssh-key-path ${SSH_KEY_PATH} \
                             --branch main \
                             --get-controller-traces \
                             --output-dir ${output_dir} \
                             --no-build
                             #  --pull

  popd 
}

# Run the experiments
for config in "${CONFIGS[@]}"
do
  echo "Running experiment: $config"
  run_exp $config
  echo "Experiment completed: $config"
done
echo "All experiments completed"
echo "Results are in: ${CURRENT_OUTPUT_DIR}"

# Plot the results
echo "Plotting results"
./plot.sh ${CURRENT_OUTPUT_DIR}
echo "Plotting completed"
echo "Plotted results are in: ${CURRENT_OUTPUT_DIR}"

echo "All Done!"
