## LoadGen

This directory contains the load generator.

**Note**
- LoadGen and all other Rust bindings only works for LTO disabled builds:
```bash
NO_LTO=1 ./scripts/build.sh clean
```

### Run LoadGen with direct SPDK access
```bash
cargo run --release --bin spdk
```

### Run LoadGen with Virtual Disk
Example:
```bash
cargo run --release --features sandook --bin synthetic
SANDOOK_CONFIG=config.json cargo run --release --features sandook --bin synthetic 192.168.128.99:5050 --mode runtime-client --config loadgen.config -p sandook --transport fake --threads 26 --samples 10 --mpps 2.0 --runtime 10 --rampup 0 -d constant --sandook_set_rate 100 --sandook_nsectors 8388608  --barrier-peers 2 --leader
SANDOOK_CONFIG=config.json cargo run --release --features sandook --bin synthetic 192.168.128.99:5050 --mode runtime-client --config loadgen.config -p sandook --transport fake --threads 26 --samples 10 --mpps 2.0 --runtime 10 --rampup 0 -d constant --sandook_set_rate 100 --sandook_nsectors 8388608 --leader-ip=192.168.128.24
```

#### Local vs Remote
- To run Virtual Disk on the local SSD, modify the `config.json` and set the following:
`kVirtualDiskType: "Local"`

- To run Virtual Disk with the remote backend, modify the `config.json` and set the following:
`kVirtualDiskType: "Remote"`
