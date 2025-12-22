#!/bin/bash
set -xe

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../
DASHBOARD_DIR=${ROOT_DIR}/dashboard

PY_VENV=".sandook_dashboard"

cd $DASHBOARD_DIR

. $PY_VENV/bin/activate
python index.py "$@"
