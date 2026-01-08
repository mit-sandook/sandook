import pandas as pd
from pathlib import Path
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import os
import glob


kP50 = "Median"
kP90 = "90th"
kP99 = "99th"


SATURATION_LATENCY_US = 1000
PROFILE_COL = "99th"


model_inputs_dir = "data/ssd_models"
profile_inputs_dir = "data/ssd_profiles"
outputs_dir = "data/ssd_data_plots"


def get_metric(filename):
    stem = Path(filename).stem
    metric = kP99
    if '100r' in stem:
        # For read-only profiles, the p99 captures a better model.
        metric = kP99
    print(f"Using metric {metric} for {filename}")
    return metric


def get_max_latency_iops(access_pattern):
    max_latency = SATURATION_LATENCY_US
    if access_pattern == "300w":
        max_iops = 1600000
    elif access_pattern == "500w":
        max_iops = 800000
    elif access_pattern == "700w":
        max_iops = 700000
    elif access_pattern == "1000w":
        max_iops = 1000000
    elif access_pattern == "100r":
        max_iops = 1700000
    else:
        max_iops = 700000
    return max_latency, max_iops


def get_workload_config(path):
    filename = os.path.basename(path).split(".")[0]
    tokens = filename.split("_")
    device_serial_num = tokens[0]
    access_pattern = tokens[1]
    return device_serial_num, access_pattern


def get_title(access_pattern):
    """
    Note:
        access_pattern is of the form 100r or 100w
    """
    ratio = int(access_pattern[:-1])
    access_op = access_pattern[-1]
    if access_op == "r":
        access_op = "Reads"
    elif access_op == "w":
        access_op = "Writes"
    else:
        raise Exception(f"Invalid access operation: {access_op}")
    return f"Workload: {ratio}% {access_op}"


def get_subtitle(disk_serial_num):
    return f"({disk_serial_num})"


def plot_model(path):
    print(f"Plotting model: {path}")

    # Load data
    df = pd.read_csv(path)
    # Model files have 2 columns: Load,<metric>. Use whatever metric was generated.
    col = df.columns[1]

    # Workload config
    disk_serial_num, access_pattern = get_workload_config(path)

    max_latency, max_iops = get_max_latency_iops(access_pattern)

    # Create a bar chart for IOPS
    fig, ax = plt.subplots(figsize=(6, 4))

    # Plot IOPS
    p90 = ax.plot(df["Load"], df[col], color="r", label=col)

    ax.grid()
    ax.set_xlabel("Load (Million IOPS)")
    ax.set_ylabel("Latency (us)")
    plt.xticks(rotation=0)

    if max_iops is not None:
        ax.set_xlim(0, max_iops)

    if max_latency is not None:
        ax.set_ylim(0, max_latency)

    def iops_fmt_(x):
        return x / 1e6

    ax.xaxis.set_major_formatter(
        ticker.FuncFormatter(lambda x, pos: (f"%.1f" % (iops_fmt_(x))))
    )

    # Add a legend
    ax.legend(loc="lower right")

    # Show the plot
    main_title = get_title(access_pattern)
    subtitle = get_subtitle(disk_serial_num)
    title = f"{main_title}\n{subtitle}"
    plt.title(title)
    plt.tight_layout(pad=0)

    # Save the plot
    filename = f"{disk_serial_num}_{access_pattern}"
    pdf_filepath = os.path.join(outputs_dir, f"{filename}_model.pdf")
    png_filepath = os.path.join(outputs_dir, f"{filename}_model.png")

    filepaths = [pdf_filepath, png_filepath]
    for filepath in filepaths:
        if os.path.exists(filepath):
            os.remove(filepath)

    fig.savefig(pdf_filepath)
    fig.savefig(png_filepath, dpi=500)
    plt.close(fig)


def plot_profile(path):
    print(f"Plotting profile: {path}")

    # Load data
    df = pd.read_csv(path)

    # Workload config
    disk_serial_num, access_pattern = get_workload_config(path)

    max_latency, max_iops = get_max_latency_iops(access_pattern)

    # Create a bar chart for IOPS
    fig, ax = plt.subplots(figsize=(6, 4))

    # Plot IOPS
    p90 = ax.plot(df["Actual"], df[PROFILE_COL], color="r", label=PROFILE_COL)

    ax.grid()
    ax.set_xlabel("Load (Million IOPS)")
    ax.set_ylabel("Latency (us)")
    plt.xticks(rotation=0)

    if max_iops is not None:
        ax.set_xlim(0, max_iops)

    if max_latency is not None:
        ax.set_ylim(0, max_latency)

    def iops_fmt_(x):
        return x / 1e6

    ax.xaxis.set_major_formatter(
        ticker.FuncFormatter(lambda x, pos: (f"%.1f" % (iops_fmt_(x))))
    )

    # Add a legend
    ax.legend(loc="lower right")

    # Show the plot
    main_title = get_title(access_pattern)
    subtitle = get_subtitle(disk_serial_num)
    title = f"{main_title}\n{subtitle}"
    plt.title(title)
    plt.tight_layout(pad=0)

    # Save the plot
    filename = f"{disk_serial_num}_{access_pattern}"
    pdf_filepath = os.path.join(outputs_dir, f"{filename}_profile.pdf")
    png_filepath = os.path.join(outputs_dir, f"{filename}_profile.png")

    filepaths = [pdf_filepath, png_filepath]
    for filepath in filepaths:
        if os.path.exists(filepath):
            os.remove(filepath)

    fig.savefig(pdf_filepath)
    fig.savefig(png_filepath, dpi=500)
    plt.close(fig)


def plot_all_profiles_(access_pattern, paths):
    def get_label_(path):
        filename = os.path.basename(path).split(".")[0]
        tokens = filename.split("_")
        device_serial_num = tokens[0]
        return device_serial_num

    # Create a bar chart for IOPS
    fig, ax = plt.subplots(figsize=(6, 4))

    for path in paths:
        # Load data
        df = pd.read_csv(path)

        # Workload config
        disk_serial_num, access_pattern = get_workload_config(path)

        max_latency, max_iops = get_max_latency_iops(access_pattern)

        # Plot IOPS
        label = get_label_(path)
        data = ax.plot(df["Actual"], df[PROFILE_COL], label=label)

    ax.grid()
    ax.set_xlabel("Load (Million IOPS)")
    ax.set_ylabel(f"Latency (us) [{PROFILE_COL}]")
    plt.xticks(rotation=0)

    if max_iops is not None:
        ax.set_xlim(0, max_iops)

    if max_latency is not None:
        ax.set_ylim(0, max_latency)

    def iops_fmt_(x):
        return x / 1e6

    ax.xaxis.set_major_formatter(
        ticker.FuncFormatter(lambda x, pos: (f"%.1f" % (iops_fmt_(x))))
    )

    # Add a legend
    ax.legend(loc="lower right")

    # Show the plot
    main_title = get_title(access_pattern)
    subtitle = ""
    title = f"{main_title}\n{subtitle}"
    plt.title(title)
    plt.tight_layout(pad=0)

    # Save the plot
    filename = f"SSDs_{access_pattern}"
    pdf_filepath = os.path.join(outputs_dir, f"{filename}_profile.pdf")
    png_filepath = os.path.join(outputs_dir, f"{filename}_profile.png")

    filepaths = [pdf_filepath, png_filepath]
    for filepath in filepaths:
        if os.path.exists(filepath):
            os.remove(filepath)

    fig.savefig(pdf_filepath)
    fig.savefig(png_filepath, dpi=500)
    plt.close(fig)


def plot_all_models_(access_pattern, paths):
    def get_label_(path):
        filename = os.path.basename(path).split(".")[0]
        tokens = filename.split("_")
        device_serial_num = tokens[0]
        return device_serial_num

    # Create a bar chart for IOPS
    fig, ax = plt.subplots(figsize=(6, 4))

    for path in paths:
        # Load data
        df = pd.read_csv(path)
        col = df.columns[1]

        # Workload config
        disk_serial_num, access_pattern = get_workload_config(path)

        max_latency, max_iops = get_max_latency_iops(access_pattern)

        # Plot IOPS
        label = get_label_(path)
        data = ax.plot(df["Load"], df[col], label=label)

    ax.grid()
    ax.set_xlabel("Load (Million IOPS)")
    ax.set_ylabel(f"Latency (us) [{col}]")
    plt.xticks(rotation=0)

    if max_iops is not None:
        ax.set_xlim(0, max_iops)

    if max_latency is not None:
        ax.set_ylim(0, max_latency)

    def iops_fmt_(x):
        return x / 1e6

    ax.xaxis.set_major_formatter(
        ticker.FuncFormatter(lambda x, pos: (f"%.1f" % (iops_fmt_(x))))
    )

    # Add a legend
    ax.legend(loc="lower right")

    # Show the plot
    main_title = get_title(access_pattern)
    subtitle = ""
    title = f"{main_title}\n{subtitle}"
    plt.title(title)
    plt.tight_layout(pad=0)

    # Save the plot
    filename = f"SSDs_{access_pattern}"
    pdf_filepath = os.path.join(outputs_dir, f"{filename}_model.pdf")
    png_filepath = os.path.join(outputs_dir, f"{filename}_model.png")

    filepaths = [pdf_filepath, png_filepath]
    for filepath in filepaths:
        if os.path.exists(filepath):
            os.remove(filepath)

    fig.savefig(pdf_filepath)
    fig.savefig(png_filepath, dpi=500)
    plt.close(fig)


def compute_total_load_(access_pattern, paths):
    def get_label_(path):
        filename = os.path.basename(path).split(".")[0]
        tokens = filename.split("_")
        device_serial_num = tokens[0]
        return device_serial_num

    total_load = 0
    for path in paths:
        # Load data
        df = pd.read_csv(path)
        col = df.columns[1]

        device_serial_num = get_label_(path)

        saturation_row = df[df[col] > SATURATION_LATENCY_US]
        if len(saturation_row) > 0:
            saturation_idx = saturation_row.iloc[0].name
            df = df.truncate(after=saturation_idx - 1)

        # Last row
        row = df.iloc[-1]
        load = row["Load"]
        print(f"{device_serial_num}: {load}")
        total_load += load
    print(f"{access_pattern} = {total_load}")
    print("===")


def plot_all_models(paths):
    paths_by_access_pattern = {}
    for path in paths:
        filename = os.path.basename(path).split(".")[0]
        tokens = filename.split("_")
        device_serial_num = tokens[0]
        access_pattern = tokens[1]
        if access_pattern not in paths_by_access_pattern:
            paths_by_access_pattern[access_pattern] = []
        paths_by_access_pattern[access_pattern].append(path)
    for access_pattern, paths in paths_by_access_pattern.items():
        plot_all_models_(access_pattern, paths)
        compute_total_load_(access_pattern, paths)


def plot_all_profiles(paths):
    paths_by_access_pattern = {}
    for path in paths:
        filename = os.path.basename(path).split(".")[0]
        tokens = filename.split("_")
        device_serial_num = tokens[0]
        access_pattern = tokens[1]
        if access_pattern not in paths_by_access_pattern:
            paths_by_access_pattern[access_pattern] = []
        paths_by_access_pattern[access_pattern].append(path)
    for access_pattern, paths in paths_by_access_pattern.items():
        plot_all_profiles_(access_pattern, paths)


def work_models():
    files = glob.glob(model_inputs_dir + "/*.model")
    for file in files:
        plot_model(file)
    plot_all_models(files)


def work_profiles():
    files = glob.glob(profile_inputs_dir + "/*.csv")
    for file in files:
        plot_profile(file)
    plot_all_profiles(files)


if __name__ == "__main__":
    work_models()
    work_profiles()
