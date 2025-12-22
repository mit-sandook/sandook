import os
import shutil
import tempfile


OUTPUT_PATH = os.path.join(tempfile.gettempdir(), "sandook", "plots")
shutil.rmtree(OUTPUT_PATH, ignore_errors=True)
os.makedirs(OUTPUT_PATH)

DEFAULT_TELEMETRY_ROOT = "/dev/shm/sandook"
CONTROLLER_RW_ISOLATION_STREAM_NAME = "controller_rw_isolation_default"
SYSTEM_LOAD_STREAM_NAME = "system_load_default"
DISK_SERVER_STREAM_NAME_PREFIX = "disk_server_*"
IO_STREAM_NAME_PREFIX = "io_*"

# Microseconds in a second
MICRO = 1000000
# Milliseconds in a second
MILLI = 1000
# Microseconds in a millisecond
MICRO_TO_MILLI = 1000

# This should be in sync with ServerMode in sandook
MIX_MODE = 0
READ_MODE = 1
WRITE_MODE = 2

# Write output figures to png files? (in addition to web rendering)
WRITE_FIGS_TO_DISK = True
# Resolution of figures written as png files
FIG_SCALE = 2

# Fraction of IOs from the trace to plot (to keep a responsive web UI)
SAMPLING_FRACTION = 0.05
