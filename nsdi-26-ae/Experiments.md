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

## Experiment 1: End-to-End (E2E)

This experiment compares the end-to-end performance of Sandook compared with two baselines, static routing (*Static Rt.*) and read/write segregation (*R/W Seg.*).

### Testbed

- We use a testbed of 8 SSDs installed in 4 servers.
- We use 1 additional server as a controller and 2 additional servers as load generating clients.
- The same server as the controller is used as the head-node to orchestrate the experiment (any server can be used for this).

### Instructions

To simplify the process of running the experiment with the three configurations (Sandook, Static Routing and Read/Write Segregation) we have packaged everything into a single bash script [experiments/exp_e2e/run.sh](experiments/exp_e2e/run.sh).

This script deploys the most recent code on to all the servers, starts the controller, the disk servers, and the load generating clients.
Depending on the configuration being evaluated, it uses the relevant `.toml` file to specify the details of the experiment ([experiments/exp_e2e/sandook.toml](experiments/exp_e2e/sandook.toml), [experiments/exp_e2e/static_rt.toml](experiments/exp_e2e/static_rt.toml), [experiments/exp_e2e/rw_seg.toml](experiments/exp_e2e/rw_seg.toml)).

```
cd experiments/exp_e2e
./run.sh
```
