from argparse import ArgumentParser
from .context import constants as ct
from .exp_utils import ExperimentConfig
import getpass
import toml


def get_config_(path: str, get_passphrase: bool) -> (dict, str):
    if get_passphrase:
        passphrase = getpass.getpass("Enter passphrase for SSH key (if any): ")
    else:
        passphrase = None
    config = toml.load(path)
    return config, passphrase


def get_args_():
    parser = ArgumentParser(description="sandook: loadgen")
    parser.add_argument(
        "--config", type=str, default="config.toml", help="Path to config file"
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default=ct.LOCAL_OUTPUT_DIR,
        help="Path to store experiment output files",
    )
    parser.add_argument(
        "--user",
        type=str,
        required=True,
        help="User to use for SSH into remote machines",
    )
    parser.add_argument(
        "--get-passphrase",
        default=False,
        action="store_true",
        help="Prompt for SSH key passphrase",
    )
    parser.add_argument(
        "--ssh-key-path",
        type=str,
        default=None,
        help="Path to SSH private key",
    )
    parser.add_argument(
        "--branch", type=str, default=ct.DEFAULT_BRANCH, help="sandook branch"
    )
    parser.add_argument(
        "--clean",
        default=False,
        action="store_true",
        help="Force a clean install/build",
    )
    parser.add_argument(
        "--no-build",
        default=False,
        action="store_true",
        help="Do not build the code again",
    )
    parser.add_argument(
        "--pull",
        default=False,
        action="store_true",
        help="Pull and build the latest code from the specified branch",
    )
    parser.add_argument(
        "--get-controller-traces",
        default=False,
        action="store_true",
        help="Get the detailed traces from controller",
    )
    parser.add_argument(
        "--format-disks",
        default=False,
        action="store_true",
        help="Format disks before launching disk servers",
    )
    args = parser.parse_args()
    return args


def get_config() -> ExperimentConfig:
    args = get_args_()
    toml_path = args.config
    get_passphrase = args.get_passphrase
    ssh_key_path = args.ssh_key_path
    config, passphrase = get_config_(toml_path, get_passphrase)
    exp_config = ExperimentConfig(
        config,
        args.output_dir,
        args.user,
        passphrase,
        ssh_key_path,
        args.branch,
        args.clean,
        args.no_build,
        args.pull,
        args.get_controller_traces,
        args.format_disks,
    )
    return exp_config
