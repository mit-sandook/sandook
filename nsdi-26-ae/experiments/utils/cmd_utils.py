import subprocess
import os
import re
from .log import get_logger
from fabric import Connection
from invoke import Responder

# import requests
# import json


logger = get_logger()

# Open connections to remote hosts for executing commands:
#   dict: <key = hostname (str), val = connection (fabric.Connection)>
remote_conns = {}
# SSH key passphrases on the remote machine (for use with operations like git clone):
#   dict: <key = hostname (str), val = passphrase (str)>
remote_passphrases = {}


"""
def gistid_to_url(gistid: str):
    data = requests.get(f"https://api.github.com/gists/{gistid}", stream=True)
    info = data.json()["files"]
    filename = list(info.keys())[0]
    return info[filename]["raw_url"]
"""


def run_cmd(cmd, output_file=None):
    """
    Run a command as a subprocess.

    cmd:         Command to execute as a string or list (with each
                 space-separated component as a different element).
    output_file: Path to an output file to store stdout of the executing
                 command. This will append to the file if it already exists or
                 create a new one if it does not exist.
                 Optional parameter; if not specified, stdout is logged.

    Returns True if error code is zero, False otherwise.
    """
    if isinstance(cmd, list):
        cmd = [s if isinstance(s, str) else str(s) for s in cmd]
        cmd = " ".join(cmd)
    logger.debug(cmd)
    if output_file is not None:
        f = open(output_file, "a+")
        output_dest = f
    else:
        output_dest = subprocess.PIPE
    proc = subprocess.Popen(
        cmd, stdout=output_dest, stderr=output_dest, universal_newlines=True, shell=True
    )
    proc.wait()
    stdout, stderr = proc.communicate()
    if output_file is not None:
        f.close()
    else:
        if len(stdout) > 0:
            logger.debug(stdout)
        if len(stderr) > 0:
            logger.debug(stderr)
    if proc.returncode != 0:
        logger.error(f"Process exitted with error code: {proc.returncode}")
        return False
    return True


def run_cmd_with_output(cmd):
    """
    Run a command as a subprocess.

    cmd:         Command to execute as a string or list (with each
                 space-separated component as a different element).

    Returns a tuple:
        (status, stdout, stderr)
        where:
            status is True if error code is zero, False otherwise.
            stdout and stderr are strings.
    """
    if isinstance(cmd, list):
        cmd = [s if isinstance(s, str) else str(s) for s in cmd]
        cmd = " ".join(cmd)
    logger.debug(cmd)
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        shell=True,
    )
    proc.wait()
    stdout, stderr = proc.communicate()
    if len(stdout) > 0:
        logger.debug(stdout)
    if len(stderr) > 0:
        logger.debug(stderr)
    if proc.returncode != 0:
        logger.debug(f"Process exitted with error code: {proc.returncode}")
    return (proc.returncode == 0, stdout, stderr)


def run_cmd_in_background(cmd, output_file=None):
    """
    Run a command as a subprocess in the background.

    cmd:         Command to execute as a string or list (with each
                 space-separated component as a different element).
    output_file: Path to an output file to store stdout of the executing
                 command. This will append to the file if it already exists or
                 create a new one if it does not exist.
                 Optional parameter; if not specified, stdout is ignored.

    Returns process handle if successful, None otherwise.
    """
    if isinstance(cmd, list):
        cmd = [s if isinstance(s, str) else str(s) for s in cmd]
        cmd = " ".join(cmd)
    logger.debug(cmd)
    if output_file is not None:
        f = open(output_file, "a+")
        stdout_dest = f
        stderr_dest = f
    else:
        stdout_dest = subprocess.PIPE
        stderr_dest = subprocess.PIPE
    proc = subprocess.Popen(
        cmd, stdout=stdout_dest, stderr=stderr_dest, universal_newlines=True, shell=True
    )
    return proc


def wait_for_proc(proc):
    """
    Waits for a running process to finish.

    proc: Handle of the running process.

    Returns True if successful, False otherwise.
    """
    proc.wait()
    stdout, stderr = proc.communicate()
    if len(stderr) > 0:
        logger.debug(stderr)
    if proc.returncode != 0:
        logger.error(f"process exitted with error code: {proc.returncode}")
        return False
    return True


def kill_proc(proc):
    """
    Kill a running process. Sends SIGINT.

    proc: Handle of the running process.

    Returns True if successful, False otherwise.
    """
    try:
        subprocess.check_call(["sudo", "kill", "-INT", str(proc.pid)])
    except Exception as e:
        logger.error(f"Error when terminating process: {proc.pid}")
        logger.error(e)
    return True


def connect_remote(hostname: str, user=None, ssh_key_path=None, passphrase=None):
    """
    Connect to a remote host to execute commands.
    Caches this connection for later use.

    hostname:     Hostname to connect to.
    user:         User to connect as.
                  Optional paramater.
    ssh_key_path: Path to SSH private key.
                  Optional paramater.
    passphrase:   SSH passphrase.
                  Optional paramater.
    """
    config = {}
    if ssh_key_path:
        config["key_filename"] = ssh_key_path
        logger.debug(f"Using SSH key: {ssh_key_path}")
    if passphrase:
        config["passphrase"] = passphrase
    try:
        conn = Connection(hostname, user=user, connect_kwargs=config)
    except Exception as e:
        raise Exception(f"Cannot connect to: {hostname} ({e})")
    remote_conns[hostname] = conn
    if passphrase:
        remote_passphrases[hostname] = passphrase
    logger.debug(f"Connected to: {hostname}")


def close_remote(hostname):
    """
    Close connection to remote host.

    hostname: Hostname of remote connection to close.

    Returns True if successful, False otherwise.
    """
    if hostname not in remote_conns:
        logger.error(f"Connection not found: {hostname}")
        return False
    conn = remote_conns.pop(hostname)
    conn.close()
    return True


def run_cmd_remote_(conn, cmd, asynchronous=False, hide=False, watchers=None):
    try:
        if isinstance(cmd, list):
            cmd = [s if isinstance(s, str) else str(s) for s in cmd]
            cmd = " ".join(cmd)
        logger.debug(f"[{conn.host}] {cmd}")
        return conn.run(
            cmd,
            warn=True,
            asynchronous=asynchronous,
            pty=True,
            hide=hide,
            watchers=watchers,
        )
    except Exception as e:
        logger.error(f"[{conn.host}] Error when running command: {e}")
        raise e

def run_cmd_remote(hostname, cmd):
    """
    Run a command on remote host.

    hostname: Hostname of remote connection to close.
    cmd:      Command to execute as a string or list (with each
              space-separated component as a different element).

    Returns True if successful, False otherwise.
    """
    if hostname not in remote_conns:
        logger.error(f"Connection not found: {hostname}")
        return False
    if hostname in remote_passphrases:
        passphrase_input = Responder(
            pattern=r"passphrase",
            response=f"{remote_passphrases[hostname]}\n",
        )
        watchers = [passphrase_input]
    else:
        watchers = None
    result = run_cmd_remote_(remote_conns[hostname], cmd, watchers=watchers)
    return result.exited == 0


def run_cmd_remote_with_output(hostname, cmd):
    """
    Run a command on remote host.

    hostname: Hostname of remote connection to close.
    cmd:      Command to execute as a string or list (with each
              space-separated component as a different element).

    Returns a tuple:
        (status, stdout, stderr)
        where:
            status is True if error code is zero, False otherwise.
            stdout and stderr are strings.
    """
    if hostname not in remote_conns:
        logger.error(f"Connection not found: {hostname}")
        return False
    conn = remote_conns[hostname]
    if hostname in remote_passphrases:
        passphrase_input = Responder(
            pattern=r"passphrase",
            response=f"{remote_passphrases[hostname]}\n",
        )
        watchers = [passphrase_input]
    else:
        watchers = None
    result = run_cmd_remote_(conn, cmd, hide=True, watchers=watchers)
    if result.return_code != 0:
        logger.debug(
            f"[{conn.host}] Process exitted with error code: \
                {result.return_code}"
        )
    return (result.return_code == 0, result.stdout, result.stderr)


def get_remote_pid(hostname, pid_basename):
    cmd = ["pidof", pid_basename]
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.warning(f"Cannot read pid for remote app {pid_basename}.")
        logger.warning(stdout)
        logger.warning(stderr)
        return []
    pid = stdout.strip()
    return [pid]


def get_remote_machine_topology(hostname):
    cmd = ["lscpu", "--parse=CPU,Core,Node"]
    status, stdout, stderr = run_cmd_remote_with_output(hostname, cmd)
    if not status:
        logger.warning(f"Cannot read machine topology for {hostname}.")
        logger.warning(stdout)
        logger.warning(stderr)
        return {}
    cpuinfo = []
    topology = {}
    for line in stdout.split("\n"):
        pattern = r"^([\d]+,[\d]+,[\d]+)"
        regex_out = re.search(pattern, line)
        if regex_out:
            cpuinfo.append(regex_out.group(1).strip().split(","))
    for cpu, core, node in cpuinfo:
        if node not in topology:
            topology[node] = {}
        if core not in topology[node]:
            topology[node][core] = {}
        topology[node][core][cpu] = cpu
    return topology


def run_cmd_remote_in_background(hostname, cmd):
    """ """
    if hostname not in remote_conns:
        logger.error(f"Connection not found: {hostname}")
        return False
    if hostname in remote_passphrases:
        passphrase_input = Responder(
            pattern=r"passphrase",
            response=f"{remote_passphrases[hostname]}\n",
        )
        watchers = [passphrase_input]
    else:
        watchers = None
    return run_cmd_remote_(
        remote_conns[hostname], cmd, asynchronous=True, watchers=watchers
    )


def get_remote_file(hostname, remote_file_path, local_file_path=None):
    """ """
    if hostname not in remote_conns:
        logger.error(f"Connection not found: {hostname}")
        return False
    return remote_conns[hostname].get(remote_file_path, local_file_path)
