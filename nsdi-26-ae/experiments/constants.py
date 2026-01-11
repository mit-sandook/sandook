import time
from pathlib import Path

USER_TAG = "<user>"

XTERM = "TERM=xterm"
CARGO_ = f"/home/{USER_TAG}/.cargo/bin/cargo"

HOME_DIR = Path.home()

# Code directories/paths
CODE_PARENT_DIR = str(HOME_DIR / "mit-sandook")
CODE_DIR = str(Path(CODE_PARENT_DIR) / "sandook")
OUTPUT_DIR = str(Path(CODE_PARENT_DIR) / "sandook-output")
timestamp = time.strftime("%Y%m%d-%H%M%S")
LOCAL_OUTPUT_DIR = str(Path(CODE_PARENT_DIR) / f"sandook-experiments-output-{timestamp}")
TRACES_DIR = "/dev/shm/sandook"
VIRTUAL_DISK_RUST_BINDINGS_DIR = str(Path(CODE_DIR) / "sandook" / "virtual_disk" / "rust_bindings")
LOADGEN_DIR = str(Path(CODE_DIR) / "loadgen")

# Build/config template directories/paths
BUILD_DIR = str(Path(CODE_DIR) / "build")
SANDOOK_CONFIG_PATH = str(Path(BUILD_DIR) / "config.json")
VIRTUAL_DISK_DIR = str(Path(BUILD_DIR) / "sandook" / "virtual_disk")
VIRTUAL_DISK_CALADAN_CONFIG_PATH = str(Path(VIRTUAL_DISK_DIR) / "virtual_disk.config")

# Code repository
REPOSITORY_URL = "https://github.com/mit-sandook/sandook.git"
DEFAULT_BRANCH = "main"

# Directories
SANDOOK_SCRIPTS_DIR = str(Path("scripts"))

# Scripts
SANDOOK_INSTALL_DEPS_SCRIPT = str(Path(SANDOOK_SCRIPTS_DIR) / "install_deps.sh")
SANDOOK_BUILD_SCRIPT = str(Path(SANDOOK_SCRIPTS_DIR) / "build.sh")
SANDOOK_RUN_SCRIPT = str(Path(SANDOOK_SCRIPTS_DIR) / "run.sh")
SANDOOK_SETUP_SCRIPT = str(Path(SANDOOK_SCRIPTS_DIR) / "setup.sh")
SANDOOK_FORMAT_DISK_SCRIPT = str(Path(SANDOOK_SCRIPTS_DIR) / "format_disk.sh")
SANDOOK_RESET_SCRIPT = str(Path(SANDOOK_SCRIPTS_DIR) / "reset.sh")
SANDOOK_TRIM_NVME_SCRIPT = str(Path(SANDOOK_SCRIPTS_DIR) / "trim_nvme.sh")

# Sandook block device
SANDOOK_MOUNT_POINT = "/mnt/sandook"
