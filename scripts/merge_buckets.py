import argparse
import os
from collections import defaultdict


def get_args():
    """
    Get arguments.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input-dir",
        "-i",
        type=str,
        required=True,
        dest="input_dir",
        help="Director of input files.",
    )
    return parser.parse_args()


def get_all_files(input_dir):
    """
    Get files in input_dir.
    """
    files = []
    for file in os.listdir(input_dir):
        if file.startswith("loadgen") and file.endswith(".txt"):
            files.append(os.path.abspath(os.path.join(input_dir, file)))
    return files


def percentile(data: dict, percentile):
    """
    Find percentile.
    """
    packet_count = sum(data["Buckets"].values())
    drop_count = data["Dropped"]
    idx = (int(packet_count + drop_count) * percentile) / 100

    seen = 0
    for k in data["Buckets"].keys():
        seen += data["Buckets"][k]
        if seen >= idx:
            return k


def process_file_per_epoch(files):
    """
    Process files per epoch.
    """
    data = {
        "Distribution": [],
        "Target": [],
        "Actual": [],
        "Dropped": [],
        "Never Sent": [],
        "Start": [],
        "StartTsc": [],
        "Buckets": defaultdict(list),
    }
    for file in files:
        first_line = True
        with open(file, "r") as f:
            for line in f:
                line = line.strip()
                if line.startswith("Opcode"):
                    continue
                # first data line then process
                if first_line:
                    data["Distribution"].append(line.split(",")[0])
                    data["Target"].append(int(line.split(",")[1]))
                    data["Actual"].append(int(line.split(",")[2]))
                    data["Dropped"].append(int(line.split(",")[3]))
                    data["Never Sent"].append(int(line.split(",")[4]))
                    data["Start"].append(int(line.split(",")[5]))
                    data["StartTsc"].append(int(line.split(",")[6]))
                    first_line = False

                # process buckets
                latency = float(line.split(",")[7])
                frequency = line.split(",")[8]
                if data["Buckets"][latency]:
                    data["Buckets"][latency] += int(frequency)
                else:
                    data["Buckets"][latency] = int(frequency)

    # Just for printing; no statistical significance
    data["Distribution"] = data["Distribution"][0]
    data["Start"] = data["Start"][0]
    data["StartTsc"] = data["StartTsc"][0]

    # Load related information should be summed
    data["Target"] = sum(data["Target"])
    data["Actual"] = sum(data["Actual"])
    data["Dropped"] = sum(data["Dropped"])
    data["Never Sent"] = sum(data["Never Sent"])

    percentile_50 = percentile(data, 50)
    percentile_90 = percentile(data, 90)
    percentile_99 = percentile(data, 99)
    percentile_99_9 = percentile(data, 99.9)
    percentile_99_99 = percentile(data, 99.99)
    # "Distribution, Target, Actual, Dropped, Never Sent, Median, 90th, 99th, 99.9th, 99.99th, Start, StartTsc"
    return data["Distribution"], data["Target"], data["Actual"], data["Dropped"], data["Never Sent"], percentile_50, percentile_90, percentile_99, percentile_99_9, percentile_99_99, data["Start"], data["StartTsc"]


def process_loadgen_files(files):
    for epoch in range(1, 100):
        files_per_epoch = []
        for file in files:
            if "epoch{}".format(epoch) in file:
                files_per_epoch.append(file)
        if len(files_per_epoch) > 0:
            resp = process_file_per_epoch(files_per_epoch)
            print(", ".join(str(x) for x in resp))


def main(args):
    """
    main.
    """
    input_filepaths = get_all_files(args.input_dir)
    process_loadgen_files(input_filepaths)


if __name__ == "__main__":
    """
    Script assumes that all loadgen files are in the same directory.
    """
    main(get_args())
