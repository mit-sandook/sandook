from __future__ import annotations

import argparse
import glob
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import numpy as np
import pandas as pd

# Input profile columns.
K_ACTUAL = "Actual"
K_DROPPED = "Dropped"

# Latency columns (as produced by the profiling scripts).
K_P50 = "Median"
K_P90 = "90th"
K_P99 = "99th"

# This should match the C++ disk model saturation threshold (`kSaturationLatencyUs`).
DEFAULT_SATURATION_LATENCY_US = 1000


@dataclass(frozen=True)
class WorkloadHeuristics:
    """
    Heuristics that shape a profile into a disk model:
    - metric: which percentile column to use
    - dampen: whether to "force" a knee near saturation to avoid optimistic tails
    """

    metric: str
    start_dampen_load_ratio: float
    num_dampen_points: int


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Generate Sandook SSD .model files from CSV profiles")
    p.add_argument("--inputs-dir", default="data/ssd_profiles", help="Directory with *.csv profile inputs")
    p.add_argument("--outputs-dir", default="data/ssd_models", help="Directory to write *.model outputs")
    p.add_argument(
        "--metric",
        default="p90",
        choices=["auto", "p50", "p90", "p99"],
        help="Which latency percentile to use. Default is p90. 'auto' uses a heuristic (p99 for 100r/1000w, else p90).",
    )
    p.add_argument(
        "--saturation-latency-us",
        type=int,
        default=DEFAULT_SATURATION_LATENCY_US,
        help="Latency (us) treated as saturation/knee.",
    )
    p.add_argument(
        "--extend-tail",
        action="store_true",
        default=False,
        help="Extend the model with a synthetic high-load tail (default: disabled).",
    )
    p.add_argument(
        "--no-extend-tail",
        dest="extend_tail",
        action="store_false",
        help="Disable synthetic tail extension.",
    )
    p.add_argument(
        "--tail-load-multiplier",
        type=float,
        default=2.0,
        help="Extend model max load to this multiple of the last measured load (default: 2.0).",
    )
    p.add_argument(
        "--tail-points",
        type=int,
        default=25,
        help="Number of synthetic tail points to add (default: 25).",
    )
    p.add_argument(
        "--tail-latency-multiplier",
        type=float,
        default=10.0,
        help="End-of-tail latency as a multiple of saturation latency (default: 10.0).",
    )
    return p.parse_args()


def metric_from_arg(arg: str) -> str:
    if arg == "p50":
        return K_P50
    if arg == "p90":
        return K_P90
    if arg == "p99":
        return K_P99
    raise ValueError(f"Unknown metric arg: {arg}")


def pick_heuristics(filename: str, metric_arg: str) -> WorkloadHeuristics:
    stem = Path(filename).stem

    # Metric selection.
    if metric_arg == "auto":
        # Existing behavior: be more conservative for pure read / pure write.
        metric = K_P99 if ("1000w" in stem or "100r" in stem) else K_P90
    else:
        metric = metric_from_arg(metric_arg)

    # Dampening configuration (existing heuristic).
    start_dampen_load_ratio = 0.95
    num_dampen_points = 5
    if "1000w" in stem:
        start_dampen_load_ratio = 0.85
        num_dampen_points = 10
    elif "300w" in stem:
        # Do not dampen.
        start_dampen_load_ratio = 1.0
        num_dampen_points = 0

    return WorkloadHeuristics(
        metric=metric,
        start_dampen_load_ratio=start_dampen_load_ratio,
        num_dampen_points=num_dampen_points,
    )


def load_profile_csv(path: str) -> pd.DataFrame:
    print(f"Reading: {path}")
    return pd.read_csv(path, skipinitialspace=True)


def iter_monotonic_profile_points(df: pd.DataFrame, latency_col: str) -> Iterable[tuple[int, float]]:
    """
    Convert a raw profile to a monotonic load->latency sequence:
    - ignore rows with drops
    - enforce non-decreasing load and non-decreasing latency
    - skip NaN/inf latencies
    """

    prev_load = -1
    prev_lat = -1.0
    for _, row in df.iterrows():
        dropped = row.get(K_DROPPED, 0)
        if pd.notna(dropped) and float(dropped) > 0:
            continue

        load_raw = row.get(K_ACTUAL)
        lat_raw = row.get(latency_col)
        if pd.isna(load_raw) or pd.isna(lat_raw) or np.isinf(lat_raw):
            continue

        # Profiles should be integer IOPS loads; cast defensively.
        try:
            load = int(load_raw)
        except Exception:
            load = int(float(load_raw))
        lat = float(lat_raw)

        if load < prev_load:
            continue
        if lat < prev_lat:
            continue

        prev_load = load
        prev_lat = lat
        yield load, lat


def dampen_profile(
    points: list[tuple[int, float]],
    saturation_latency_us: int,
    start_dampen_load_ratio: float,
    num_dampen_points: int,
) -> list[tuple[int, float]]:
    """
    "Dampening" here means: ensure the model has a clear knee near saturation.

    Why: the C++ DiskModel treats >= saturation latency as "at/over peak". If the
    measured profile never reaches (or cleanly crosses) that threshold, peak IOPS
    may be overly optimistic. Dampening forces a short synthetic ramp up to
    saturation near the end of the curve.
    """

    if not points:
        return []

    # Keep only points strictly below saturation (stop at first > saturation).
    below: list[tuple[int, float]] = []
    max_load = 0
    max_lat = 0.0
    for load, lat in points:
        if lat > saturation_latency_us:
            break
        below.append((load, lat))
        max_load = load
        max_lat = lat

    if not below:
        # If everything is already > saturation, keep the very first point.
        load, lat = points[0]
        return [(load, lat)]

    do_dampen = (start_dampen_load_ratio != 1.0) and (num_dampen_points != 0)
    if not do_dampen:
        # Ensure we have at least one "saturation" point at the end.
        if max_lat < saturation_latency_us:
            below.append((max_load, float(saturation_latency_us)))
        return below

    start_load = int(max_load * start_dampen_load_ratio)
    end_load = max_load
    end_lat = float(saturation_latency_us)

    # Start the ramp at max(existing_latency, 70% of saturation) to preserve monotonicity.
    start_lat = max(max_lat, float(saturation_latency_us) * 0.70)

    # Drop all points after start_load; we replace that tail with a synthetic ramp.
    prefix: list[tuple[int, float]] = []
    for load, lat in below:
        if load >= start_load:
            break
        prefix.append((load, lat))

    # Linear ramp from (start_load, start_lat) to (end_load, end_lat).
    if num_dampen_points <= 0 or end_load <= start_load:
        return prefix + [(end_load, end_lat)]

    for i in range(num_dampen_points):
        pct = float(i) / float(num_dampen_points)
        load = int(start_load + (end_load - start_load) * pct)
        lat = start_lat + (end_lat - start_lat) * pct
        prefix.append((load, lat))
    prefix.append((end_load, end_lat))

    return prefix


def extend_profile_tail(
    points: list[tuple[int, float]],
    saturation_latency_us: int,
    tail_load_multiplier: float,
    tail_points: int,
    tail_latency_multiplier: float,
) -> list[tuple[int, float]]:
    """
    Add a synthetic tail beyond the last modeled load to avoid DiskModel's
    hard extrapolation behavior (if cur_load > max_load => last_latency * 10).

    We do NOT need new profiling for this: we just extend the curve smoothly in
    a monotonic way, so interpolation is used for higher loads.
    """

    if not points:
        return []
    if tail_points <= 0:
        return points
    if tail_load_multiplier <= 1.0:
        return points

    last_load, last_lat = points[-1]
    target_load = int(float(last_load) * float(tail_load_multiplier))
    if target_load <= last_load:
        return points

    start_lat = max(float(last_lat), float(saturation_latency_us))
    end_lat = float(saturation_latency_us) * float(tail_latency_multiplier)
    if end_lat < start_lat:
        end_lat = start_lat

    tail: list[tuple[int, float]] = []
    for i in range(1, tail_points + 1):
        pct = float(i) / float(tail_points)
        load = int(float(last_load) + (float(target_load - last_load) * pct))
        lat = start_lat + (end_lat - start_lat) * pct
        if load <= last_load:
            continue
        # Ensure strictly increasing loads.
        if tail and load <= tail[-1][0]:
            load = tail[-1][0] + 1
        tail.append((load, lat))

    if not tail:
        return points
    return points + tail


def write_model_file(outputs_dir: str, output_filename: str, latency_col: str, points: list[tuple[int, float]]):
    os.makedirs(outputs_dir, exist_ok=True)
    filepath = os.path.join(outputs_dir, output_filename)

    # Header is mostly for humans/plotting; DiskModel ignores it.
    lines = [f"Load,{latency_col}\n"]
    lines.extend([f"{load},{lat}\n" for (load, lat) in points])

    if os.path.exists(filepath):
        os.remove(filepath)
    with open(filepath, "w+", encoding="utf-8") as f:
        f.writelines(lines)
    print(f"Output written to: {filepath}")


def main() -> int:
    args = parse_args()

    files = glob.glob(os.path.join(args.inputs_dir, "*.csv"))
    if not files:
        raise RuntimeError(f"No input CSV files found in {args.inputs_dir}")

    for path in files:
        heur = pick_heuristics(path, args.metric)
        print(f"Using metric {heur.metric} for {path}")

        df = load_profile_csv(path)
        points = list(iter_monotonic_profile_points(df, heur.metric))
        points = dampen_profile(
            points,
            saturation_latency_us=args.saturation_latency_us,
            start_dampen_load_ratio=heur.start_dampen_load_ratio,
            num_dampen_points=heur.num_dampen_points,
        )
        if args.extend_tail:
            points = extend_profile_tail(
                points,
                saturation_latency_us=args.saturation_latency_us,
                tail_load_multiplier=args.tail_load_multiplier,
                tail_points=args.tail_points,
                tail_latency_multiplier=args.tail_latency_multiplier,
            )

        out_name = f"{Path(path).stem}.model"
        write_model_file(args.outputs_dir, out_name, heur.metric, points)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
