#!/usr/bin/python3
import time
from .context import get_logger
from .context import get_config
from .context import ExperimentConfig
from .context import Experiment
from .context import Client
from .context import Loadgen

from threading import Thread


logger = get_logger()


def task(exp: Experiment, client: Client):
    logger.info(client.ip)
    loadgen = Loadgen(exp, client)
    loadgen.run()

def run(config: ExperimentConfig):
    client_output_dirs = []
    with Experiment(config) as exp:
        logger.info("Experiment ready to run")
        threads = []
        for client in exp.clients:
            client_output_dirs.append(client.get_local_output_dir())
            t = Thread(
                target=task,
                args=(
                    exp,
                    client,
                ),
            )
            threads.append(t)
            t.start()
            time.sleep(2)
        for t in threads:
            t.join()
        logger.info("Experiment completed")
    logger.debug(f"Client outputs: {client_output_dirs}")
    Loadgen.merge_loadgen_buckets(config.local_output_dir, client_output_dirs)


if __name__ == "__main__":
    exp_config = get_config()
    run(exp_config)
