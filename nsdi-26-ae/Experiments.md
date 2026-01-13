# Experiments

## Install

Only on the head-node (`zg03.pdos.csail.mit.edu`):

```
cd experiments
./scripts/install_deps.sh
```

We have provided scripts to run and analyze the results of two key experiments.

## Note
> [!NOTE]
> The experiment results are very specific to the available SSDs and their wear-and-tear condition.
> The results in the paper were done on a (partially) different set of SSDs and servers therefore the exact results will not be possible to replicate now.
> (the availability of the hardware and the wear-and-tear of the SSDs has changed since the experiments in the paper.)
> Nevertheless, the key trends should still be reproducible.

## Experiment 1: [End-to-End (E2E)](experiments/exp_e2e)

This experiment compares the end-to-end performance of Sandook compared with two baselines, static routing (*Static Rt.*) and read/write segregation (*R/W Seg.*).

### Testbed

- We use a testbed of 8 SSDs installed in 4 servers.
- We use 1 additional server as a controller and 2 additional servers as load generating clients.
- The same server as the controller is used as the head-node to orchestrate the experiment (any server can be used for this).

### Instructions

To simplify the process of running the experiment with the three configurations (Sandook, Static Routing and Read/Write Segregation) we have packaged everything into a single bash script [run.sh](experiments/exp_e2e/run.sh).

This script deploys the most recent code on to all the servers, starts the controller, the disk servers, and the load generating clients.
Depending on the configuration being evaluated, it uses the relevant `.toml` file to specify the details of the experiment ([sandook.toml](experiments/exp_e2e/sandook.toml), [static_rt.toml](experiments/exp_e2e/static_rt.toml), [rw_seg.toml](experiments/exp_e2e/rw_seg.toml)).

```
cd experiments/exp_e2e
./run.sh
```

## Experiment 2: [Read-Write Ratios (RW Ratio)](experiments/exp_rw_ratio)

This experiment compares the raw storage performance of Sandook compared with the static routing (*Static Rt.*) baseline on the following read/write ratios:
- 10% Reads, 90% Writes
- 30% Reads, 70% Writes
- 50% Reads, 50% Writes


### Testbed

- We use a testbed of 8 SSDs installed in 4 servers.
- We use 1 additional server as a controller and 2 additional servers as load generating clients.
- The same server as the controller is used as the head-node to orchestrate the experiment (any server can be used for this).

### Instructions

To simplify the process of running the experiment with the two configurations (Sandook and Static Routing) we have packaged everything into a single bash script [run.sh](experiments/exp_rw_ratio/run.sh).

This script deploys the most recent code on to all the servers, starts the controller, the disk servers, and the load generating clients.
Depending on the configuration being evaluated, it uses the relevant `.toml` file to specify the details of the experiment (e.g., [100w_sandook.toml](experiments/exp_rw_ratio/100w_sandook.toml), [500w_static_rt.toml](experiments/exp_rw_ratio/500w_static_rt.toml) etc.)

```
cd experiments/exp_rw_ratio
./run.sh
```

## Outputs

After running each experiment, you should look for the latest (timestamped) output in the following directory:

```
cd experiments/output
```

This will contain the raw logs as well as any relevant plots/figures.