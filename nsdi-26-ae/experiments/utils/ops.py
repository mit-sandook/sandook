from .context import constants as ct
from .cmd_utils import run_cmd
from .cmd_utils import run_cmd_with_output
from .cmd_utils import get_remote_file
from .cmd_utils import run_cmd_remote
from .cmd_utils import run_cmd_remote_with_output
from .log import get_logger
from datetime import datetime
import os


logger = get_logger()


def setup_experiment(
    hostname: str,
    net_iface: str,
    local_output_dir: str,
    user: str,
    branch: str,
    clean: bool,
    no_build: bool,
    pull: bool,
    is_client: bool,
):
    clean_stale_traces(hostname)
    setup_parent_dir_(hostname)
    setup_output_dir_(hostname)
    setup_local_output_dir_(local_output_dir)
    setup_repository_(hostname, user, branch, clean, no_build, pull, is_client)
    teardown_experiment(hostname)
    stop_iokerneld(hostname)
    setup_machine_(hostname, net_iface)
    disable_dvfs(hostname)


def teardown_experiment(hostname: str):
    logger.debug("Tearing down...")
    cmd = [ct.XTERM, f"cd {ct.CODE_DIR};", f"./{ct.SANDOOK_RUN_SCRIPT} teardown"]
    status, _, _ = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        raise Exception("Cannot teardown experiment")
    logger.debug(f"Experiment teardown successful: {hostname}")


def clean_stale_traces(hostname: str):
    logger.debug("Cleaning stale traces...")
    cmd = [f"sudo rm -rf {ct.TRACES_DIR}/*"]
    status, _, _ = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        raise Exception("Cannot clean stale traces")
    logger.debug(f"Cleaning stale traces successful: {hostname}")


def start_iokerneld(hostname: str, net_iface: str):
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"./{ct.SANDOOK_RUN_SCRIPT} iokerneld {net_iface}",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception("Cannot start iokerneld")
    logger.debug(f"Launch iokerneld successful: {hostname}")


def stop_iokerneld(hostname: str):
    cmd = [
        ct.XTERM,
        "sudo pkill -9 iokerneld;",
    ]
    _, _, _ = run_cmd_remote_with_output(hostname, cmd)
    logger.debug(f"Killed iokerneld on: {hostname}")


def disable_dvfs(hostname: str):
    cmd = [
        ct.XTERM,
        "echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor;",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception("Cannot disable DVFS")
    logger.debug(f"Disabled DVFS on: {hostname}")


def setup_machine_(hostname: str, net_iface: str):
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"./{ct.SANDOOK_SETUP_SCRIPT} {net_iface}",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception(f"Cannot setup machine: {hostname}")
    logger.debug(f"Machine setup successful: {hostname}")


def start_controller(
    hostname: str,
    net_iface: str,
    controller_ip: str,
    control_plane_sched_type: str,
    data_plane_sched_type: str,
    disk_server_rejections: bool,
):
    disk_server_rejections_int = 1 if disk_server_rejections else 0
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"./{ct.SANDOOK_RUN_SCRIPT} controller {net_iface} {ct.OUTPUT_DIR} {controller_ip} {control_plane_sched_type} {data_plane_sched_type} {disk_server_rejections_int}",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception("Cannot start controller")


def format_disk(hostname: str, disk_pci: str):
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"./{ct.SANDOOK_FORMAT_DISK_SCRIPT} {disk_pci};",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception("Cannot format disk_server")


def start_disk_server(
    hostname: str, net_iface: str, disk_pci: str, disk_ip: str, controller_ip: str
):
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"./{ct.SANDOOK_RUN_SCRIPT} disk_server {net_iface} {ct.OUTPUT_DIR} {disk_pci} {disk_ip} {controller_ip}",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception("Cannot start disk_server")


def start_storage_perf(hostname: str, net_iface: str, disk_pci: str):
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"./{ct.SANDOOK_RUN_SCRIPT} storage_perf {net_iface} {ct.OUTPUT_DIR} {disk_pci}",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception("Cannot start storage_perf")


def start_disk_pre_fill(hostname: str, net_iface: str, disk_pci: str):
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"./{ct.SANDOOK_RUN_SCRIPT} disk_pre_fill {net_iface} {ct.OUTPUT_DIR} {disk_pci}",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception("Cannot start disk_pre_fill")


def start_blk_dev(hostname: str, blk_dev_ip: str, affinity_list: str):
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"./{ct.SANDOOK_RUN_SCRIPT} blk_dev {ct.OUTPUT_DIR} {blk_dev_ip} {affinity_list}",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception("Cannot start blk_dev")


def mount_ublk_dev(hostname: str, ublk_id="ublkb0"):
    cmd = [
        ct.XTERM,
        f"sudo mkfs.ext4 /dev/{ublk_id} -D;",
        f"sudo fsck.ext4 /dev/{ublk_id};",
        f"sudo mkdir -p {ct.SANDOOK_MOUNT_POINT};",
        f"sudo mount /dev/{ublk_id} {ct.SANDOOK_MOUNT_POINT}/;",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception("Cannot mount blk_dev")


def umount_ublk_dev(hostname: str):
    cmd = [
        ct.XTERM,
        f"sudo umount {ct.SANDOOK_MOUNT_POINT}/",
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)


def get_cargo_path(user: str):
    cargo = ct.CARGO_.replace(ct.USER_TAG, user)
    return cargo


def clean_repo_(hostname: str):
    logger.debug("Cleaning repository...")
    cmd = f"rm -rf {ct.CODE_DIR}"
    run_cmd_remote(hostname, cmd)


def repo_exists_(hostname: str):
    cmd = f"[ -d {ct.CODE_DIR} ]"
    exists = run_cmd_remote(hostname, cmd)
    logger.debug(f"Repository exists: {exists}")
    return exists


def repo_branch_matches_(hostname: str, branch: str):
    cmd = f"cd {ct.CODE_DIR}; git branch --show-current;"
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception(f"Cannot check repository branch: {hostname}")
    return stdout.strip() == branch


def pull_repo_(hostname: str, branch: str):
    logger.debug("Pulling repository...")
    cmd = [
        f"cd {ct.CODE_DIR};",
        f"git fetch;",
        f"git checkout {branch};",
        f"git checkout .;",
        "git pull --rebase;",
    ]
    success = run_cmd_remote(hostname, cmd)
    if not success:
        raise Exception(f"Cannot pull repository: {ct.REPOSITORY_URL} {hostname}")
    logger.debug(f"Pulled repository: {hostname}")


def clone_repo_(hostname: str, branch: str):
    logger.debug("Cloning repository...")
    add_github_to_known_hosts_(hostname)
    cmd = [
        f"cd {ct.CODE_PARENT_DIR};",
        f"git clone {ct.REPOSITORY_URL};",
        f"cd {ct.CODE_DIR};",
        f"git checkout {branch};",
        f"git checkout .;",
        "git pull --rebase;",
    ]
    success = run_cmd_remote(hostname, cmd)
    if not success:
        raise Exception(f"Cannot clone repository: {ct.REPOSITORY_URL}")
    logger.debug(f"Cloned repository: {hostname}")


def install_repo_(hostname: str):
    logger.debug("Installing...")
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"./{ct.SANDOOK_INSTALL_DEPS_SCRIPT};",
    ]
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.info(stdout)
        logger.error(stderr)
        raise Exception("Cannot install repository")
    logger.debug(f"Install repository successful: {hostname}")


def build_repo_(hostname: str, user: str, is_client: bool):
    if is_client:
        cargo = get_cargo_path(user)
        build_repo__(hostname, no_LTO=True)
        build_virtual_disk_rust_bindings_(hostname, cargo)
        build_loadgen_(hostname, cargo)
    build_repo__(hostname, no_LTO=False)


def build_virtual_disk_rust_bindings_(hostname: str, cargo: str):
    logger.debug(f"Building virtual disk Rust bindings...")
    cmd = [
        ct.XTERM,
        f"cd {ct.VIRTUAL_DISK_RUST_BINDINGS_DIR};",
        f"{cargo} clean;",
        f"{cargo} build --release",
    ]
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.info(stdout)
        logger.error(stderr)
        raise Exception("Cannot build virtual disk Rust bindings")
    logger.debug(f"Build virtual disk Rust bindings successful: {hostname}")


def build_loadgen_(hostname: str, cargo: str):
    logger.debug(f"Building loadgen...")
    cmd = [
        ct.XTERM,
        f"cd {ct.LOADGEN_DIR};",
        f"{cargo} clean;",
        f"{cargo} build --release",
    ]
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.info(stdout)
        logger.error(stderr)
        raise Exception("Cannot build loadgen")
    logger.debug(f"Build loadgen successful: {hostname}")


def add_github_to_known_hosts_(hostname: str):
    logger.debug("Adding GitHub to known hosts...")
    cmd = "ssh-keyscan -H github.com >> ~/.ssh/known_hosts"
    success = run_cmd_remote(hostname, cmd)
    if not success:
        raise Exception(f"Cannot add GitHub to known hosts: {hostname}")
    logger.debug(f"Added GitHub to known hosts: {hostname}")


def setup_repository_(
    hostname: str, user: str, branch: str, clean: bool, no_build: bool, pull: bool, is_client: bool
):
    if clean:
        clean_repo_(hostname)
    if not repo_exists_(hostname):
        clone_repo_(hostname, branch)
        install_repo_(hostname)
    if pull or not repo_branch_matches_(hostname, branch):
        pull_repo_(hostname, branch)
    if clean or not no_build or pull:
        build_repo_(hostname, user, is_client)
    logger.debug(f"Setup repository successful: {hostname}")


def setup_parent_dir_(hostname: str):
    cmd = [f"mkdir -p {ct.CODE_PARENT_DIR}"]
    success = run_cmd_remote(hostname, cmd)
    if not success:
        raise Exception(f"Cannot create parent directory: {hostname}")


def setup_local_output_dir_(output_dir):
    cmd = [f"rm -rf {output_dir};", f"mkdir -p {output_dir}"]
    success = run_cmd(cmd)
    if not success:
        raise Exception(f"Cannot create local output directory: {output_dir}")


def setup_output_dir_(hostname: str):
    cmd = [f"sudo rm -rf {ct.OUTPUT_DIR};", f"mkdir {ct.OUTPUT_DIR}"]
    success = run_cmd_remote(hostname, cmd)
    if not success:
        raise Exception(f"Cannot create output directory: {hostname}")
    logger.debug(f"Created output directory: {hostname}")


def create_client_sandook_config_with_params(
    hostname: str, ip: str, controller_ip: str
) -> str:
    filename = f"config_{get_datetime_()}.json"
    config_file_path = os.path.join(ct.OUTPUT_DIR, filename)
    cmd = [
        f"cp {ct.SANDOOK_CONFIG_PATH} {config_file_path};",
        f'sed -i "s/\\"kVirtualDiskIP\\": \\"[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*\\"/\\"kVirtualDiskIP\\": \\"{ip}\\"/g" {config_file_path};',
        f'sed -i "s/\\"kControllerIP\\": \\"[0-9]*\.[0-9]*\.[0-9]*\.[0-9]*\\"/\\"kControllerIP\\": \\"{controller_ip}\\"/g" {config_file_path}',
    ]
    success = run_cmd_remote(hostname, cmd)
    if not success:
        raise Exception(f"Cannot create Sandook config file: {hostname}")
    return config_file_path


def create_client_caladan_config_with_params(hostname: str, ip: str, cores: int) -> str:
    filename = f"virtual_disk_{get_datetime_()}.config"
    config_file_path = os.path.join(ct.OUTPUT_DIR, filename)
    cmd = [
        f"cp {ct.VIRTUAL_DISK_CALADAN_CONFIG_PATH} {config_file_path};",
        f'sed -i "s/host_addr [0-9]*\.[0-9]*\.[0-9]*\.[0-9]*/host_addr {ip}/g" {config_file_path};',
        f'sed -i "s/runtime_kthreads [0-9]*/runtime_kthreads {cores}/g" {config_file_path};',
        f'sed -i "s/runtime_spinning_kthreads [0-9]*/runtime_spinning_kthreads {cores}/g" {config_file_path};',
        f'sed -i "s/runtime_guaranteed_kthreads [0-9]*/runtime_guaranteed_kthreads {cores}/g" {config_file_path}',
    ]
    success, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not success:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception(f"Cannot create Caladan config file: {hostname}")
    return config_file_path


def build_repo__(hostname: str, no_LTO: bool):
    logger.debug(f"Building (LTO: {not no_LTO})...")
    if no_LTO:
        prefix = "NO_LTO=1"
    else:
        prefix = ""
    cmd = [
        ct.XTERM,
        f"cd {ct.CODE_DIR};",
        f"{prefix} ./{ct.SANDOOK_BUILD_SCRIPT} clean;",
    ]
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.info(stdout)
        logger.error(stderr)
        raise Exception("Cannot build repository")
    logger.debug(f"Build repository successful: {hostname}")


def get_datetime_() -> str:
    return str(datetime.now().strftime("%Y%m%d_%H%M%S_%f"))


def get_experiment_output(hostname: str, local_output_dir: str):
    compress_filepath = f"{ct.OUTPUT_DIR}.tar.gz"
    cmd = [
        f"rm -rf {compress_filepath};",
        f"tar -cvf {compress_filepath} {ct.OUTPUT_DIR}",
    ]
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception(f"Cannot compress output directory: {hostname}")
    local_output_dir = f"{local_output_dir}/{hostname}"
    cmd = [f"rm -rf {local_output_dir}; mkdir -p {local_output_dir}"]
    status, stdout, stderr = run_cmd_with_output(cmd)
    if not status:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception(f"Cannot remove local directory: {local_output_dir}")
    local_output_filepath = f"{local_output_dir}/output.tar.gz"
    try:
        status, stdout, stderr = run_cmd_remote_with_output(
            hostname, f"echo {compress_filepath}"
        )
        if not status:
            logger.error(stdout)
            logger.error(stderr)
            raise Exception(
                f"Failed during copy from {compress_filepath} to {local_output_filepath}..."
            )
        remote_path = stdout.strip()
        status = get_remote_file(hostname, remote_path, local_output_filepath)
        if not status:
            raise Exception(f"Cannot get compressed output: {hostname}")
    except Exception as e:
        raise Exception(f"Cannot get files: {hostname} {e}")
    cmd = [f"cd {local_output_dir};", f"tar -xvf {local_output_filepath}"]
    status, stdout, stderr = run_cmd_with_output(cmd)
    if not status:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception(f"Cannot decompress: {local_output_filepath}")
    logger.debug(f"Got output from: {hostname}")


def gather_controller_output(hostname: str):
    cmd = [
        f"cp {ct.TRACES_DIR}/* {ct.OUTPUT_DIR}",
    ]
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception(f"Cannot gather traces from controller: {hostname}")


def gather_client_output(hostname: str):
    cmd = [
        f"cp {ct.TRACES_DIR}/* {ct.OUTPUT_DIR}",
    ]
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.error(stdout)
        logger.error(stderr)
        raise Exception(f"Cannot gather traces from client: {hostname}")
