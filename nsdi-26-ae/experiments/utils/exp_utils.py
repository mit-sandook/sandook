from .cmd_utils import connect_remote
from .cmd_utils import close_remote
from .cmd_utils import run_cmd_remote_with_output
from .cmd_utils import run_cmd_remote
from .cmd_utils import run_cmd
from .cmd_utils import run_cmd_with_output
from .cmd_utils import get_remote_machine_topology
from .ops import create_client_caladan_config_with_params
from .ops import create_client_sandook_config_with_params
from .ops import format_disk
from .ops import setup_experiment
from .ops import start_blk_dev
from .ops import mount_ublk_dev
from .ops import umount_ublk_dev
from .ops import start_controller
from .ops import start_iokerneld
from .ops import stop_iokerneld
from .ops import start_disk_server
from .ops import start_storage_perf
from .ops import start_disk_pre_fill
from .ops import teardown_experiment
from .ops import get_cargo_path
from .ops import get_experiment_output
from .ops import gather_controller_output
from .ops import gather_client_output
from .log import get_logger
from threading import Thread
import re
import time
import statistics


logger = get_logger()


class Client:
    def __init__(
        self, hostname: str, config: dict, controller_ip: str, local_output_dir: str
    ):
        self.hostname = hostname
        self.local_output_dir = f"{local_output_dir}/{hostname}"
        self.ip = config["ip"]
        self.cores = int(config["cores"])
        self.app = config["app"]
        self.args = config["args"]
        self.net_iface = config["net_iface"]
        self.blk_dev_id = 0

        # UBLK device related args
        self.with_ublk_dev = config.get("with_ublk_dev", False)
        self.wait_ublk_dev_sec = config.get("wait_ublk_dev_sec", 30)
        self.mount_ublk = config.get("mount_ublk", False)
        self.num_ublk_devs = config.get("num_ublk_devs", 0)
        self.ublk_dev_args = config.get("ublk_dev_args", [""])

        self.caladan_config_path = create_client_caladan_config_with_params(
            self.hostname, self.ip, self.cores
        )
        self.sandook_config_path = create_client_sandook_config_with_params(
            self.hostname, self.ip, controller_ip
        )
        self.topology = get_remote_machine_topology(self.hostname)

    def pre_launch_setup(self):
        if self.with_ublk_dev:
            self.check_ublk_status()
            self.setup_blk_dev()
            if self.mount_ublk:
                self.mount_blk_dev(self.hostname)

    def get_local_output_dir(self):
        return self.local_output_dir

    def check_ublk_status(self):
        cmd = "sudo modprobe ublk_drv"
        status = run_cmd_remote_with_output(self.hostname, cmd)
        if not status:
            raise Exception(f"Client {self.hostname} failed to load ublk_drv")

    def setup_blk_dev(self):
        for i in range(self.num_ublk_devs):
            ip = None
            if isinstance(self.ip, list):
                ip = self.ip[i]
            else:
                ip = self.ip
            start_blk_dev(self.hostname, ip, self.ublk_dev_args[i])
            self.get_blk_dev_id()

    def get_blk_dev_id(self):
        cmd = "sudo lsblk -d -o name | grep ublk"
        timeout = 0
        while timeout < self.wait_ublk_dev_sec:
            status, stdout, stderr = run_cmd_remote_with_output(self.hostname, cmd)
            if status:
                self.blk_dev_id = stdout.strip()
                break
            timeout += 1
            time.sleep(1)

        if not self.blk_dev_id:
            raise Exception(f"Failed to get blk_dev ID on {self.hostname}")

    def mount_blk_dev(self, hostname: str):
        mount_ublk_dev(hostname, self.blk_dev_id)

    def set_shield_on_app_cores(self):
        cores = self.get_ht(self.cores)
        cmd = [
            f"sudo cset shield --reset;",
            f"sudo cset shield --cpu={','.join(cores)} -k on;",
        ]
        status = run_cmd_remote(self.hostname, cmd)
        if not status:
            logger.error(f"Failed to set shield on {self.hostname}")

    def available_ht(self):
        cpus = 0
        for node in self.topology.keys():
            for core in self.topology[node].keys():
                cpus += len(self.topology[node][core].keys())
        return cpus

    def unused_ht(self):
        unused_ht = []
        for node in self.topology.keys():
            for core in self.topology[node].keys():
                for ht in self.topology[node][core].keys():
                    unused_ht.append(ht)
        return unused_ht

    def get_ht(self, n_ht: int):
        if n_ht > self.available_ht():
            raise Exception(f"Requested cores ({n_ht}) > available cores")

        cpus = []
        sockets = list(self.topology.keys())
        for socket in sockets:
            cores = list(self.topology[socket].keys())
            for core in cores:
                hts = self.topology[socket].pop(core)
                for t in list(hts.keys()):
                    cpus.append(t)
                    if len(cpus) == n_ht:
                        return cpus

    def teardown(self):
        if self.with_ublk_dev and self.mount_ublk:
            umount_ublk_dev(self.hostname)


class ExperimentConfig:
    def __init__(
        self,
        config: dict,
        local_output_dir: str,
        user: str,
        passphrase: str,
        ssh_key_path: str,
        branch: str,
        clean: bool,
        no_build: bool,
        pull: bool,
        get_controller_traces: bool,
        format_disks: bool,
    ):
        self.local_output_dir = local_output_dir
        self.user = user
        self.passphrase = passphrase
        self.ssh_key_path = ssh_key_path
        self.branch = branch
        self.clean = clean
        self.no_build = no_build
        self.pull = pull
        self.get_controller_traces = get_controller_traces
        self.format_disks = format_disks
        self.hostnames = []
        self.net_ifaces = []
        self.controller = None
        self.controller_ip = None
        self.controller_config = None
        self.disk_servers = {}
        self.client_hostnames = []
        self.client_configs = []
        for machine in config["machines"]:
            host_info = config["machines"][machine]
            hostname = host_info["host"]
            self.hostnames.append(hostname)
            self.net_ifaces.append(host_info["net_iface"])
            for role in host_info["roles"]:
                config_args = host_info[role]
                config_args["net_iface"] = host_info["net_iface"]
                if "controller" in role:
                    self.controller = hostname
                    self.controller_ip = config_args["ip"]
                    self.controller_config = config_args
                if "disk_server" in role:
                    if hostname not in self.disk_servers:
                        self.disk_servers[hostname] = []
                    self.disk_servers[hostname].append(config_args)
                if "client" in role:
                    self.client_hostnames.append(hostname)
                    self.client_configs.append(config_args)


class Experiment:
    """
    Context manager for running an experiment with setup/teardown.
    """

    def __init__(self, config: ExperimentConfig):
        self.config = config
        self.client_handles = []

    def __enter__(self):
        self.connect_servers()
        self.setup_experiment()
        self.pre_launch()
        self.format_disks()
        self.start_controller()
        self.start_disk_servers()
        self.setup_clients()
        return self

    def __exit__(self, etype, value, traceback):
        self.teardown_experiment()

    @property
    def clients(self):
        return self.client_handles

    def connect_servers(self):
        for hostname in self.config.hostnames:
            connect_remote(
                hostname,
                user=self.config.user,
                ssh_key_path=self.config.ssh_key_path,
                passphrase=self.config.passphrase
            )

    def setup_experiment(self):
        threads = []
        for hostname, net_iface in zip(self.config.hostnames, self.config.net_ifaces):
            is_client = hostname in self.config.client_hostnames
            is_disk_server = hostname in self.config.disk_servers
            disk_pcis = []
            if is_disk_server:
                disk_pcis = [cfg["disk_pci"] for cfg in self.config.disk_servers[hostname]]
            t = Thread(
                target=setup_experiment,
                args=(
                    hostname,
                    net_iface,
                    self.config.local_output_dir,
                    self.config.user,
                    self.config.branch,
                    self.config.clean,
                    self.config.no_build,
                    self.config.pull,
                    is_client,
                    is_disk_server,
                    disk_pcis,
                ),
            )
            threads.append(t)
            t.start()
        for t in threads:
            t.join()
        logger.info("Experiment setup successful")

    def pre_launch(self):
        self.pre_launch_disk_servers()

    def teardown_experiment(self):
        # Teardown in this particular order: clients, disk_servers, controller
        teardown_hostnames = []
        teardown_hostnames.extend(self.config.client_hostnames)
        teardown_hostnames.extend(self.config.disk_servers.keys())
        teardown_hostnames.append(self.config.controller)
        for hostname in teardown_hostnames:
            teardown_experiment(hostname)
        if self.config.get_controller_traces:
            gather_controller_output(hostname)
        for hostname in self.config.client_hostnames:
            gather_client_output(hostname)
        for hostname in self.config.hostnames:
            get_experiment_output(hostname, self.config.local_output_dir)
            close_remote(hostname)
        n_disks = 0
        for _, configs in self.config.disk_servers.items():
            n_disks += len(configs)
        self.log_disk_server_stats()
        self.check_and_log_controller_stats(n_disks)

    def start_controller(self):
        net_iface = self.config.controller_config["net_iface"]
        ip = self.config.controller_ip
        cp_sched = self.config.controller_config["control_plane_scheduler_type"]
        dp_sched = self.config.controller_config["data_plane_scheduler_type"]
        disk_server_rejections = self.config.controller_config["disk_server_rejections"]
        start_controller(
            self.config.controller,
            net_iface,
            ip,
            cp_sched,
            dp_sched,
            disk_server_rejections,
        )

    @staticmethod
    def format_disk_(hostname, config):
        disk_pci = config["disk_pci"]
        format_disk(hostname, disk_pci)

    @staticmethod
    def pre_launch_disk_server_(hostname, config):
        net_iface = config["net_iface"]
        disk_pci = config["disk_pci"]
        pre_launch = config.get("pre_launch", None)
        if pre_launch:
            if pre_launch == "storage_perf":
                start_storage_perf(hostname, net_iface, disk_pci)
            elif pre_launch == "format_pre_fill":
                Experiment.format_disk_(hostname, config)
                start_disk_pre_fill(hostname, net_iface, disk_pci)
            else:
                raise Exception(f"Unknown pre_launch: {pre_launch}")

    @staticmethod
    def pre_launch_disk_servers_(hostname, configs):
        for config in configs:
            Experiment.pre_launch_disk_server_(hostname, config)

    @staticmethod
    def start_disk_server_(hostname, config, controller_ip):
        ip = config["ip"]
        net_iface = config["net_iface"]
        disk_pci = config["disk_pci"]
        start_disk_server(hostname, net_iface, disk_pci, ip, controller_ip)

    @staticmethod
    def start_disk_servers_(hostname, configs, controller_ip):
        for config in configs:
            Experiment.start_disk_server_(hostname, config, controller_ip)

    @staticmethod
    def format_disks_(hostname, configs):
        for config in configs:
            Experiment.format_disk_(hostname, config)

    def format_disks(self):
        if not self.config.format_disks:
            return
        threads = []
        for hostname, configs in self.config.disk_servers.items():
            t = Thread(
                target=Experiment.format_disks_,
                args=(
                    hostname,
                    configs,
                ),
            )
            threads.append(t)
            t.start()
        for t in threads:
            t.join()

    def start_disk_servers(self):
        threads = []
        for hostname, configs in self.config.disk_servers.items():
            t = Thread(
                target=Experiment.start_disk_servers_,
                args=(
                    hostname,
                    configs,
                    self.config.controller_config["ip"],
                ),
            )
            threads.append(t)
            t.start()
        for t in threads:
            t.join()

    def pre_launch_disk_servers(self):
        threads = []
        for hostname, configs in self.config.disk_servers.items():
            t = Thread(
                target=Experiment.pre_launch_disk_servers_,
                args=(
                    hostname,
                    configs,
                ),
            )
            threads.append(t)
            t.start()
        for t in threads:
            t.join()

    def start_iokerneld_on_client(self, client: Client):
        if client.hostname == self.config.controller:
            logger.debug(
                f"Controller already launched iokerneld on: {self.config.controller}"
            )
            return
        stop_iokerneld(client.hostname)
        args = client.net_iface
        start_iokerneld(client.hostname, args)

    def setup_clients(self):
        for hostname, config in zip(
            self.config.client_hostnames, self.config.client_configs
        ):
            client = Client(
                hostname,
                config,
                self.config.controller_ip,
                self.config.local_output_dir,
            )
            self.client_handles.append(client)
            self.start_iokerneld_on_client(client)

    def run_cmd_on_client(self, client: Client, cmd: [str]):
        return run_cmd_remote(client.hostname, cmd)

    def log_disk_server_stats(self):
        cmd = [
            f'for i in $(find {self.config.local_output_dir} -name "disk_server*.log" -type f); '
            "do echo $i && cat $i | "
            'grep -e Serial -e Pure -e Impure -e "Failed reads" -e "Failed writes" -e "Mixed"; done'
        ]
        run_cmd(cmd)

    def check_and_log_controller_stats(self, expected_n_disks):
        def get_iops_stats_(op):
            if op != "read" and op != "write" and op != "mix":
                raise Exception(f"Invalid operation: {op}")
            cmd = [
                f'for i in $(find {self.config.local_output_dir} -name "controller*.log" -type f); '
                "do echo $i && cat $i | "
                f'grep -e "Peak IOPS ({op})"; done'
            ]
            status, stdout, stderr = run_cmd_with_output(cmd)
            if not status:
                if len(stderr) > 0:
                    logger.error(stderr)
                return None
            iops = []
            total_peak_iops = 0
            lines = stdout.split("\n")
            for line in lines:
                matches = re.search(r"(.*) Peak IOPS \(.*\): (\d+)", line)
                if matches:
                    match = int(matches.group(2))
                    total_peak_iops += match
                    iops.append(match)
                else:
                    continue
            n_disks = len(iops)
            stdev = statistics.stdev(iops)
            return total_peak_iops, stdev, n_disks

        ops = ["read", "write", "mix"]
        for op in ops:
            stats = get_iops_stats_(op)
            if stats is None:
                continue
            total_peak_iops, stdev, n_disks = stats
            logger.info(
                f"Total IOPS for {op} = {total_peak_iops} [stdev = {stdev}] ({n_disks} SSDs)"
            )
            assert (
                n_disks == expected_n_disks
            ), "The experiment did not run with all specified disks"
