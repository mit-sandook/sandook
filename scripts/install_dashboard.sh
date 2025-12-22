#!/bin/bash
set -xe

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
DASHBOARD_DIR=${ROOT_DIR}/dashboard

PY_VENV=".sandook_dashboard"

sudo apt-get install -y python3.11 python3-venv

cd $DASHBOARD_DIR

# Create and activate Python virtual env.
python3 -m venv $PY_VENV
. $PY_VENV/bin/activate

# Install Python dependencies.
python3 -m pip install -r requirements.txt
