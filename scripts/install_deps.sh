#!/bin/bash
set -xe

# Globals
SCRIPT_DIR=$(dirname $(readlink -f $0))
ROOT_DIR=${SCRIPT_DIR}/../

if [ -f $SCRIPT_DIR/.last_install_deps ]; then
    LAST_COMMIT=$(cat $SCRIPT_DIR/.last_install_deps)
    CURRENT_COMMIT=$(git rev-parse --short HEAD)
    if [ "$LAST_COMMIT" == "$CURRENT_COMMIT" ]; then
        echo "Dependencies are up-to-date."
        exit 0
    fi
fi

# Configurations
export DEBIAN_FRONTEND=noninteractive

# Install Linux packages
function install_packages {
    sudo apt update
    sudo -E apt install -y gcc-13 g++-13 fio pkg-config libtool dwarves lshw nvme-cli pre-commit libjsoncpp-dev
}

# Install Rust (Needed for loadgen)
function install_rust {
    pushd ${SCRIPT_DIR}
    curl https://sh.rustup.rs -sSf | sh -s -- -y
    source $HOME/.cargo/env
    rustup default nightly
    rustup component add rust-src
    popd
}

# Initialize submodules
function init_submodules {
    git submodule update --init --recursive
}

install_packages
install_rust
init_submodules

cd $SCRIPT_DIR

./install_cmake.sh
./install_liburing.sh
./install_ubdsrv.sh
./install_caladan.sh
pre-commit install

git rev-parse --short HEAD > .last_install_deps
