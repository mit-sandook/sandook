from pathlib import Path
import pandas as pd
import numpy as np
import os
import glob

kActual = "Actual"
kDropped = "Dropped"

kP50 = "Median"
kP90 = "90th"
kP99 = "99th"

kInf = "inf"

inputs_dir = "data/ssd_profiles"
outputs_dir = "data/ssd_models"

SATURATION_LATENCY_US = 1000


def get_df(filename):
    print(f"Reading: {filename}")
    df = pd.read_csv(filename, skipinitialspace=True)
    return df


def get_metric(filename):
    stem = Path(filename).stem
    metric = kP90
    if '1000w' in stem:
        metric = kP99
    elif '100r' in stem:
        metric = kP99
    print(f"Using metric {metric} for {filename}")
    return metric


def get_start_dampen_load_ratio(filename):
    stem = Path(filename).stem
    start_dampen_load_ratio = 0.95
    if '1000w' in stem:
        start_dampen_load_ratio = 0.85
    elif '250w' in stem:
        # Do not dampen.
        start_dampen_load_ratio = 1.0
    return start_dampen_load_ratio


def get_num_dampen_points(filename):
    stem = Path(filename).stem
    num_dampen_points = 5
    if '1000w' in stem:
        num_dampen_points = 10
    elif '250w' in stem:
        # Do not dampen.
        num_dampen_points = 0
    return num_dampen_points


def get_profile(df, col):
    output = []
    prev_load = 0
    prev_lat = 0
    for _, row in df.iterrows():
        load = row[kActual]
        dropped = row[kDropped]
        lat = row[col]
        # Smoothen the anomalies in the profile.
        if dropped > 0:
            continue
        if load < prev_load:
            continue
        if lat < prev_lat:
            continue
        prev_load = load
        prev_lat = lat
        if pd.isna(lat) or np.isinf(lat):
            continue
        output.append(f"{load},{lat}\n")
    return output


def dampen_profile(model, start_dampen_load_ratio, num_dampen_points):
    max_load = 0
    max_load_lat = 0
    output = []
    # Filter all points that have latency higher than saturation.
    for line in model:
        [load_str, lat_str] = line.strip().split(",")
        load = int(load_str)
        lat = float(lat_str)
        if lat > SATURATION_LATENCY_US:
            break
        else:
            max_load = load
            max_load_lat = lat
            output.append(line)
    if start_dampen_load_ratio != 1.0 and num_dampen_points != 0:
        # For the last few points, dampen them such that there is very high
        # latency corresponding to their load.
        start_load = max_load * start_dampen_load_ratio
        start_lat = SATURATION_LATENCY_US * 0.70
        end_load = max_load
        end_lat = SATURATION_LATENCY_US
        chop_idx = len(output)
        for i in range(len(output)):
            line = output[i]
            [load_str, lat_str] = line.strip().split(",")
            load = int(load_str)
            lat = float(lat_str)
            if load >= start_load:
                chop_idx = i
                break
        output = output[:chop_idx]
        diff_load = (end_load - start_load) / num_dampen_points
        diff_lat = (end_lat - start_lat) / num_dampen_points
        for i in range(num_dampen_points):
            load = int(start_load + (diff_load * i))
            lat = start_lat + (diff_lat * i)
            output.append(f"{load},{lat}\n")
        output.append(f"{end_load},{end_lat}")
    else:
        if max_load_lat < SATURATION_LATENCY_US:
            output.append(f"{max_load},{SATURATION_LATENCY_US}")
    return output


def generate_model_file(profile, col, filename):
    header = f"Load,{col}\n"
    output = [header]
    output.extend(profile)
    filepath = os.path.join(outputs_dir, filename)
    if os.path.exists(filepath):
        os.remove(filepath)
    with open(filepath, "w+") as f:
        f.writelines(output)
    print(f"Output written to: {filepath}")


def generate_model_files(inputs_dir):
    files = glob.glob(inputs_dir + "/*.csv")
    for file in files:
        df = get_df(file)
        metric = get_metric(file)
        num_dampen_points = get_num_dampen_points(file)
        start_dampen_load = get_start_dampen_load_ratio(file)
        model_output_file = f"{Path(file).stem}.model"
        profile = get_profile(df, metric)
        profile = dampen_profile(profile, start_dampen_load, num_dampen_points)
        generate_model_file(profile, metric, model_output_file)


generate_model_files(inputs_dir)
