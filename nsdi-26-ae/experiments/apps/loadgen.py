import re
import os
import time
import shutil
from .context import Client, Experiment
from .context import constants as ct
from .context import get_logger
from .app import App
from .loadgen_merge_buckets import merge_buckets

from zope.interface import implementer
from pathlib import Path

logger = get_logger()

START_DELAY_SEC = 0
LOADGEN_BIN = "target/release/synthetic"
# LoadGen needs an address (this is just no-op).
# The actual address is in the config file.
NO_OP_ADDR = "192.168.128.99:5050"
LOADGEN_OUTPUT_FILENAME = "loadgen.log"
LOADGEN_FILES_PREFIX = "/tmp/loadgen*"

MERGED_OUTPUT_FILENAME = "loadgen_merged.csv"


def parse_threads_from_args(args: str):
    matches = re.search(r"--threads (\d+)", args)
    if matches:
        n_threads = int(matches.group(1))
        return n_threads
    else:
        raise Exception(f"Cannot find number of threads from args: {args}")


def get_cmd(client: Client):
    pre_proc_cmdline = f"sudo rm -rf {LOADGEN_FILES_PREFIX};"
    cmdline = (
        f"SANDOOK_CONFIG={client.sandook_config_path} "
        f"sudo -E {LOADGEN_BIN} {NO_OP_ADDR} "
        "--mode runtime-client "
        f"--config {client.caladan_config_path} "
        "-p sandook "
        "--transport fake "
        f"{client.args} "
        f"2>&1 | tee {ct.OUTPUT_DIR}/{LOADGEN_OUTPUT_FILENAME};"
    )
    post_proc_cmdline = f"sudo cp {LOADGEN_FILES_PREFIX} {ct.OUTPUT_DIR};"
    cmd = [
        f"cd {ct.LOADGEN_DIR};",
        pre_proc_cmdline,
        cmdline,
        post_proc_cmdline,
    ]
    return cmd


@implementer(App)
class Loadgen:
    def __init__(self, exp: Experiment, client: Client):
        self.exp = exp
        self.client = client

    @staticmethod
    def merge_loadgen_buckets(local_output_dir: str, client_output_dirs: [str]):
        loadgen_merged_buckets_dir = f"{local_output_dir}/loadgen_buckets"
        Path(loadgen_merged_buckets_dir).mkdir(parents=True, exist_ok=True)
        for i, client_output_dir in enumerate(client_output_dirs):
            loadgen_output_dir = Path(
                client_output_dir + os.path.expanduser(ct.OUTPUT_DIR)
            )
            for file in os.listdir(loadgen_output_dir):
                if file.startswith("loadgen_latencies") and file.endswith(".txt"):
                    src_fpath = os.path.join(loadgen_output_dir, file)
                    dst_fname = file
                    dst_fpath = os.path.join(loadgen_merged_buckets_dir, dst_fname)
                    logger.debug(f"Copying: {src_fpath} -> {dst_fpath}")
                    shutil.copyfile(src_fpath, dst_fpath)
        logger.debug(f"Merged loadgen output stored in: {loadgen_merged_buckets_dir}")
        loadgen_merged_output_filepath = os.path.join(
            loadgen_merged_buckets_dir, MERGED_OUTPUT_FILENAME
        )
        merge_buckets(loadgen_merged_buckets_dir, loadgen_merged_output_filepath)

    def run(self):
        time.sleep(START_DELAY_SEC)

        cmd = get_cmd(self.client)
        n_threads = parse_threads_from_args(self.client.args)
        assert n_threads == self.client.cores, f"Expected {self.client.cores} threads, got {n_threads}"
        logger.debug(
            f"Launching command with {n_threads} threads on: {self.client.hostname}"
        )
        status = self.exp.run_cmd_on_client(self.client, cmd)
        if not status:
            logger.error(f"Experiment ended with an error: {self.client.hostname}")
