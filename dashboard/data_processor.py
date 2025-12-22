from dash import Dash, dcc, html
import os
import dask
import dask.dataframe as dd
import glob
import shutil
import tempfile
import plotly.express as px
import numpy as np
from constants import *
from utils import compute_if_dask


median_fun = dd.Aggregation(
    name="median",
    chunk=lambda s: s.quantile(0.5),
    agg=lambda s0: s0.sum(),
)


p90_fun = dd.Aggregation(
    name="p90",
    chunk=lambda s: s.quantile(0.90),
    agg=lambda s0: s0.sum(),
)


p99_fun = dd.Aggregation(
    name="p99",
    chunk=lambda s: s.quantile(0.99),
    agg=lambda s0: s0.sum(),
)


def get_controller_rw_isolation_traces(input_path):
    controller_rw_isolation_stream_path = os.path.join(
        input_path, CONTROLLER_RW_ISOLATION_STREAM_NAME
    )
    if not os.path.isfile(controller_rw_isolation_stream_path):
        print("Controller RW isolation telemetry file not found")
        return None
    df = (
        dd.read_csv(controller_rw_isolation_stream_path)
        .sort_values(by="timestamp")
    )
    return df


def get_system_load_traces(input_path):
    system_load_stream_path = os.path.join(input_path, SYSTEM_LOAD_STREAM_NAME)
    if not os.path.isfile(system_load_stream_path):
        print("System load telemetry file not found")
        return None
    df = (
        dd.read_csv(system_load_stream_path)
        .sample(frac=SAMPLING_FRACTION)
        .sort_values(by="timestamp")
    )
    return df


def get_disk_server_traces(input_path, no_aggregate):
    DISK_SERVER_STREAM_PREFIX_PATH = os.path.join(
        input_path, DISK_SERVER_STREAM_NAME_PREFIX
    )

    def get_iops_(df):
        col = "timestamp_s"
        df_iops = df.groupby(col).agg(
            read_iops=("completed_reads", "sum"),
            write_iops=("completed_writes", "sum"),
        )
        df_iops = compute_if_dask(df_iops)
        df_iops["read_iops"] = df_iops["read_iops"] / SAMPLING_FRACTION
        df_iops["write_iops"] = df_iops["write_iops"] / SAMPLING_FRACTION
        df_iops["total_iops"] = df_iops["read_iops"] + df_iops["write_iops"]
        return df_iops

    def get_iops_per_100ms_(df):
        col = "timestamp_100ms"
        df_iops = df.groupby(col).agg(
            read_iops=("completed_reads", "sum"),
            write_iops=("completed_writes", "sum"),
        )
        df_iops = compute_if_dask(df_iops)
        df_iops["read_iops"] = (df_iops["read_iops"] / SAMPLING_FRACTION) * 10
        df_iops["write_iops"] = (df_iops["write_iops"] / SAMPLING_FRACTION) * 10
        df_iops["total_iops"] = df_iops["read_iops"] + df_iops["write_iops"]
        return df_iops

    def get_agg_stats_(df):
        col = "timestamp"
        df_agg_stats = df.groupby(col).agg(
            mode_mix=("mode_mix", "sum"),
            mode_read=("mode_read", "sum"),
            mode_write=("mode_write", "sum"),
            read_weight=("read_weight", "sum"),
            write_weight=("write_weight", "sum"),
            inflight_reads=("inflight_reads", "sum"),
            inflight_writes=("inflight_writes", "sum"),
            completed_reads=("completed_reads", "sum"),
            completed_writes=("completed_writes", "sum"),
            rejected_reads=("rejected_reads", "sum"),
            rejected_writes=("rejected_writes", "sum"),
            pure_reads=("pure_reads", "sum"),
            pure_writes=("pure_writes", "sum"),
            impure_reads=("impure_reads", "sum"),
            impure_writes=("impure_writes", "sum"),
            signal_read_latency=("signal_read_latency", "mean"),
            signal_write_latency=("signal_write_latency", "mean"),
            median_read_latency=("median_read_latency", "mean"),
            median_write_latency=("median_write_latency", "mean"),
            is_rejecting_requests=("is_rejecting_requests", "sum"),
            read_mops=("read_mops", "sum"),
            write_mops=("write_mops", "sum"),
        )
        return compute_if_dask(df_agg_stats).reset_index()

    dfs = []
    disk_server_trace_files = glob.glob(DISK_SERVER_STREAM_PREFIX_PATH)
    for f in disk_server_trace_files:
        if not os.path.isfile(f):
            continue
        trace_name = os.path.basename(f).split(".")[0]
        if trace_name == "disk_server_default":
            continue
        try:
            df = (
                dd.read_csv(
                    f,
                    dtype={
                        "read_weight": "float64",
                        "write_weight": "float64",
                        "read_mops": "float64",
                        "write_mops": "float64",
                    },
                )
                .sample(frac=SAMPLING_FRACTION)
                .sort_values(by="timestamp")
            )
        except Exception as e:
            raise Exception(f"Cannot load dataframe for: {f} {e}")
        df["mode_mix"] = df.apply(
            lambda row: 1 if row["mode"] == MIX_MODE else 0,
            axis=1,
            meta=(None, "int64"),
        )
        df["mode_read"] = df.apply(
            lambda row: 1 if row["mode"] == READ_MODE else 0,
            axis=1,
            meta=(None, "int64"),
        )
        df["mode_write"] = df.apply(
            lambda row: 1 if row["mode"] == WRITE_MODE else 0,
            axis=1,
            meta=(None, "int64"),
        )
        df["impure_reads"] = df.apply(
            lambda row: row["inflight_reads"]
            if row["mode"] != READ_MODE
            else 0,
            axis=1,
            meta=(None, "int64"),
        )
        df["pure_reads"] = df.apply(
            lambda row: row["inflight_reads"]
            if row["mode"] == READ_MODE
            else 0,
            axis=1,
            meta=(None, "int64"),
        )
        df["impure_writes"] = df.apply(
            lambda row: row["inflight_writes"]
            if row["mode"] != WRITE_MODE
            else 0,
            axis=1,
            meta=(None, "int64"),
        )
        df["pure_writes"] = df.apply(
            lambda row: row["inflight_writes"]
            if row["mode"] == WRITE_MODE
            else 0,
            axis=1,
            meta=(None, "int64"),
        )
        df["timestamp_s"] = df["timestamp"] // MICRO
        df["timestamp_100ms"] = df["timestamp"] // (MILLI * 100)
        try:
            df_iops = get_iops_(df)
        except Exception as e:
            raise Exception(f"Cannot get IOPS dataframe for: {f} {e}")
        try:
            df_iops_per_100ms = get_iops_per_100ms_(df)
        except Exception as e:
            raise Exception(f"Cannot get 100ms IOPS dataframe for: {f} {e}")
        dfs.append([df, df_iops, df_iops_per_100ms, trace_name])
        print(f"Found disk server traces at: {f}")
    if len(dfs) == 0:
        return None
    print(f"Found {len(dfs)} disk server traces")
    if not no_aggregate:
        agg_tag = "aggregate"
        df_concat = (
            dd.concat(list(map(lambda elems: elems[0], dfs)))
            .sort_values(by="timestamp")
            .reset_index()
        )
        df_agg_iops = get_iops_(df_concat)
        df_agg_iops_per_100ms = get_iops_per_100ms_(df_concat)
        df_agg_stats = get_agg_stats_(df_concat)
        dfs.append([df_agg_stats, df_agg_iops, df_agg_iops_per_100ms, agg_tag])
    return dfs
