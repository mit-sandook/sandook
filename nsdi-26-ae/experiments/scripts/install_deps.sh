#!/bin/bash
set -e

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../

PY_VENV=".sandook-nsdi-26-ae"

# Configurations
export DEBIAN_FRONTEND=noninteractive

# Install Python dependencies
function install_deps() {
  cd $ROOT_DIR

  python3 -m venv $PY_VENV
  . $PY_VENV/bin/activate
  python3 -m pip install -r requirements.txt
}

install_deps