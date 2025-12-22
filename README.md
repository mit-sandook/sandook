# Sandook
![Status](https://img.shields.io/badge/Version-Experimental-green.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## Artifact Evaluation Guide (NSDI - Fall 2026)
This document (and the references within) are to guide the artifact evaluation process.

## Overview

Sandook is a distributed storage system that aggregates multiple NVMe SSDs into a unified, high-performance block device. It features dynamic read/write workload isolation, SSD performance model-driven scheduling, and exposes storage via a standard Linux block device interface.

## Key Features

- **Distributed Storage**: Aggregates multiple SSD servers into a single virtual disk
- **Read/Write Isolation**: Dynamically partitions servers into read-only and write-only groups
- **Replication**: 2-way replication for writes across servers
- **Performance Models**: Uses profiled SSD latency-load curves to guide scheduling
- **Block Device Interface**: Exposes storage as `/dev/ublkbX` via ublksrv

## Architecture

```
┌──────────────┐     ┌────────────┐     ┌─────────────┐     ┌──────────┐
│  Application │     │ Controller │     │ Disk Server │     │  NVMe    │
│      ↓       │     │  (central) │     │   (per-SSD) │────►│  SSD     │
│  Block Dev   │◄───►│ Scheduling │◄───►│  SPDK/POSIX │     │          │
│  (ublksrv)   │     │ Allocation │     │   Backend   │     └──────────┘
│      ↓       │     └────────────┘     └─────────────┘
│ Virtual Disk │──────────────────────────────────────►
└──────────────┘              RPC (Caladan TCP)
```

## Design Details

Sandook addresses multiple sources of SSD performance variability through a two-tier scheduling architecture:

- **Control-Plane Scheduling** (at Controller): Runs at slower timescales (~100µs) with a global view. Implements read/write isolation by dynamically assigning SSDs to handle predominantly reads or writes, and uses SSD performance models to distribute load according to each disk's capacity.

- **Data-Plane Scheduling** (at Client): Runs at faster timescales with local decisions. Selects which SSD replica to read from using weighted selection, and reacts to congestion by shifting load away from slow SSDs.

- **Log-Structured Writes**: Writes can be directed to any SSD regardless of current block location, with block mappings maintained at the client. This enables maximum flexibility in write steering.

- **Congestion Control**: Each disk server monitors its latency (p99) and signals congestion state to clients, which then reduce load to that server until it recovers.

- **SSD Performance Models**: Each SSD is profiled offline to build load-latency curves for different workload mixes (read-only, write-only, 25/50/75% writes). These models guide the controller's scheduling decisions.

## Directory Structure

```
sandook/
├── sandook/                    # Core source code
│   ├── base/                   # Common types, constants, I/O descriptors
│   ├── bindings/               # C++ bindings to Caladan runtime
│   ├── blk_dev/                # Linux block device agent (ublksrv integration)
│   ├── config/                 # Runtime configuration parsing
│   ├── controller/             # Central controller: registration, allocation, scheduling
│   ├── disk_model/             # SSD performance models (load → latency curves)
│   ├── disk_server/            # Storage server: POSIX, memory, and SPDK backends
│   ├── mem/                    # Memory management (slab allocator)
│   ├── rpc/                    # TCP-based RPC layer
│   ├── samples/                # Example applications
│   ├── scheduler/              # Scheduling algorithms
│   │   ├── control_plane/      # Controller-side: R/W isolation, profile-guided
│   │   └── data_plane/         # Client-side: weighted/random server selection
│   ├── telemetry/              # Performance monitoring and metrics
│   ├── test/                   # Unit tests and benchmarks
│   ├── utils/                  # Utility programs (calibration, profiling)
│   └── virtual_disk/           # Client-side virtual disk abstraction
├── lib/                        # Dependencies
│   ├── caladan/                # High-performance userspace networking runtime
│   ├── liburing/               # io_uring library
│   ├── ubdsrv/                 # Userspace block device server
│   ├── tdigest/                # T-Digest for streaming percentiles
│   └── patches/                # Patches for dependencies
├── loadgen/                    # Rust-based load generator for benchmarking
├── data/                       # SSD profiles and models
│   ├── ssd_models/             # Pre-computed latency-load models
│   └── ssd_profiles/           # Raw SSD profiling data
├── dashboard/                  # Python visualization dashboard
└── scripts/                    # Build, setup, and test scripts
```

## Build

```bash
# Install dependencies
./scripts/install_deps.sh

# Build
./scripts/build.sh clean

# Setup (configure network interface)
./scripts/setup.sh <network_interface_name>
```

## Build Requirements

- GCC 13+ (C++23)
- CMake 3.24+
- Linux kernel 5.15+ (for ublk support)
- DPDK-compatible NIC (for Caladan)

## Hardware Requirements

### Storage
- **NVMe SSDs**: Sandook requires NVMe SSDs accessible via SPDK or standard POSIX interfaces
- **Tested Models**: The paper evaluation used **Samsung PM1725a** NVMe SSDs (3.2TB, PCIe 3.0 x8)
- **Testbed Configuration**: 10 SSDs distributed across multiple machines, with one SSD per disk server

### Network
- **NIC**: DPDK-compatible network interface card required for the Caladan runtime
- **Tested NIC**: Mellanox ConnectX-5 (100 Gbps)
- **Requirements**: The NIC must support DPDK poll-mode drivers; Caladan uses kernel-bypass for low-latency networking

### Machines
- Minimum 2 machines: one for the client (controller + block device agent) and one or more for disk servers
- Each disk server machine requires at least one NVMe SSD
- For proper benefits of the scheduling policies, have at least as many SSDs as the replication factor

## Key Configuration

Configuration files (`.config`) specify:
- Controller IP/port
- Storage server IP/port
- Scheduler type (control-plane and data-plane)
- Virtual disk type (local/remote)
- Disk server backend (POSIX/Memory/SPDK)

## Artifact Evaluation

This artifact evaluates the key claims of the Sandook paper: *"Unleashing The Potential of Datacenter SSDs by Taming Performance Variability"* (NSDI '26).

### Accessing Evaluation Testbed
> [!NOTE]
> In order to make it easier to access the hardware/software environment for evaluating the artifact, we will provide access to our own server testbed. We kindly request the authors to email us at girfan@mit.edu and ankit@cs.tufts.edu when ready to evaluate the artifact and we will promptly provide credentials and login instructions to our servers.

### Quick Start
> [!NOTE]
> Information can be found in [QuickStart](nsdi-26-ae/QuickStart.md).

### Claims Evaluated

1. **Main Result (Figure 4)**: Sandook achieves significant I/O throughput improvement over existing systems that tackle only a single source of SSD performance variability, while maintaining sub-millisecond tail latency. This experiment compares Sandook against:
   - Static striping (FDS-style)
   - Congestion control alone (Gimbal-style)
   - Read/write isolation alone (Rails-style)
   - Weighted routing alone (ReFlex-style)

2. **Impact of Replication (Figure 8)**: Demonstrates how Sandook's log-structured writes with replication enable flexible I/O steering. This experiment shows the performance trade-offs of replication factor on read/write throughput and tail latency.

### Reproducing Experiment Results
> [!NOTE]
> Information can be found in [Experiments](nsdi-26-ae/Experiments.md).
