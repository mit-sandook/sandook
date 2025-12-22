from dash import Dash, dcc, html
import os
import dask
import dask.dataframe as dd
import glob
import shutil
import tempfile
import plotly.express as px
import numpy as np
import plotly.graph_objects as go
from constants import *
from plotly.subplots import make_subplots
from utils import compute_if_dask


def plot_controller_rw_isolation_traces(df):
    if df is None:
        return []
    print("plot_controller_rw_isolation_traces: started")
    fig_id = "controller_rw_isolation_ops"
    df = compute_if_dask(df)
    metrics = [
        "is_traffic",
        "num_read_servers",
        "num_write_servers",
        "num_servers",
    ]
    fig = make_subplots(rows=1, cols=1)
    for metric in metrics:
        fig.add_trace(
            go.Scatter(x=df["timestamp"], y=df[metric], name=metric),
        )
    if WRITE_FIGS_TO_DISK:
        fig_output_path = os.path.join(OUTPUT_PATH, fig_id)
        fig.write_image(f"{fig_output_path}.png", scale=FIG_SCALE)
    print("plot_controller_rw_isolation_traces: ended")
    return [dcc.Graph(id=fig_id, figure=fig)]


def plot_system_load_traces(df):
    if df is None:
        return []
    print("plot_system_load_traces: started")
    fig_id = "system_load"
    df = compute_if_dask(df)
    metrics = [
        "read_ops",
        "write_ops",
        "total_ops",
    ]
    fig = make_subplots(rows=1, cols=1)
    for metric in metrics:
        fig.add_trace(
            go.Scatter(x=df["timestamp"], y=df[metric], name=metric),
        )
    if WRITE_FIGS_TO_DISK:
        fig_output_path = os.path.join(OUTPUT_PATH, fig_id)
        fig.write_image(f"{fig_output_path}.png", scale=FIG_SCALE)
    print("plot_system_load_traces: ended")
    return [dcc.Graph(id=fig_id, figure=fig)]


def plot_iops(df, trace_name):
    if df is None:
        return []
    print("plot_iops: started")
    fig_id = f"iops_{trace_name}"
    graphs = []
    metrics = [
        "total_iops",
        "read_iops",
        "write_iops",
    ]
    fig = make_subplots(rows=1, cols=1)
    for metric in metrics:
        fig.add_trace(
            go.Scatter(x=df.index, y=df[metric], name=metric),
        )
    fig.update_layout(
        title=f"{trace_name} IOPS",
        xaxis_title="Time (s)",
        yaxis_title="IOPS",
        hovermode=False,
    )
    if WRITE_FIGS_TO_DISK:
        fig_output_path = os.path.join(OUTPUT_PATH, fig_id)
        fig.write_image(f"{fig_output_path}.png", scale=FIG_SCALE)
    graphs.append(dcc.Graph(id=fig_id, figure=fig))
    print("plot_iops: ended")
    return graphs


def plot_iops_per_100ms(df, trace_name):
    if df is None:
        return []
    print("plot_iops_per_100ms: started")
    fig_id = f"iops_per_100ms_{trace_name}"
    graphs = []
    metrics = [
        "total_iops",
        "read_iops",
        "write_iops",
    ]
    fig = make_subplots(rows=1, cols=1)
    for metric in metrics:
        fig.add_trace(
            go.Scatter(x=df.index, y=df[metric], name=metric),
        )
    fig.update_layout(
        title=f"{trace_name} IOPS (per 100 ms)",
        xaxis_title="Time (s)",
        yaxis_title="IOPS",
        hovermode=False,
    )
    if WRITE_FIGS_TO_DISK:
        fig_output_path = os.path.join(OUTPUT_PATH, fig_id)
        fig.write_image(f"{fig_output_path}.png", scale=FIG_SCALE)
    graphs.append(dcc.Graph(id=fig_id, figure=fig))
    print("plot_iops_per_100ms: ended")
    return graphs


def plot_compare_weights(dfs):
    metrics = [
        "read_weight",
        "write_weight",
    ]
    out = []
    for metric in metrics:
        out.extend(plot_compare_weights_(dfs, metric))
    return out


def plot_compare_weights_(dfs, metric):
    if dfs is None:
        return []
    print(f"plot_compare_weights_{metric}: started")
    fig_id = f"compare_weights_{metric}"
    graphs = []
    fig = make_subplots(rows=1, cols=1)
    for df, _, _, trace_name in dfs:
        if trace_name == "aggregate":
            continue
        fig.add_trace(
            go.Scatter(x=df["timestamp"], y=df[metric], name=trace_name),
        )
        print(f"plot_compare_weights_{metric}: {trace_name}")
    fig.update_layout(
        title=f"Comparison of {metric}",
        xaxis_title="Time (s)",
        yaxis_title=metric,
        yaxis_range=[0, 1],
        hovermode=False,
    )
    if WRITE_FIGS_TO_DISK:
        fig_output_path = os.path.join(OUTPUT_PATH, fig_id)
        fig.write_image(f"{fig_output_path}.png", scale=FIG_SCALE)
    graphs.append(dcc.Graph(id=fig_id, figure=fig))
    print(f"plot_compare_weights_{metric}: ended")
    return graphs


def plot_disk_server_traces(df, trace_name):
    if df is None:
        return []
    print("plot_disk_server_traces: started")
    fig_id = f"disk_server_stats_{trace_name}"
    subplot_metrics = [
        [
            "mode_mix",
            "mode_read",
            "mode_write",
        ],
        [
            "read_weight",
            "write_weight",
        ],
        [
            "is_rejecting_requests",
        ],
        [
            "inflight_reads",
            "inflight_writes",
            "pure_reads",
            "pure_writes",
            "impure_reads",
            "impure_writes",
            "rejected_reads",
            "rejected_writes",
        ],
        [
            "signal_write_latency",
            "signal_read_latency",
        ],
        [
            "median_write_latency",
            "median_read_latency",
        ],
        [
            "read_mops",
            "write_mops",
        ],
    ]
    df = compute_if_dask(df)
    fig = make_subplots(rows=len(subplot_metrics), cols=1)
    for i, metrics in enumerate(subplot_metrics):
        if len(df) == 0:
            continue
        for metric in metrics:
            fig.add_trace(
                go.Scatter(x=df["timestamp"], y=df[metric], name=metric),
                row=i + 1,
                col=1,
            )
    fig.update_layout(title=trace_name, height=1000, hovermode=False)
    if WRITE_FIGS_TO_DISK:
        fig_output_path = os.path.join(OUTPUT_PATH, fig_id)
        fig.write_image(f"{fig_output_path}.png", scale=FIG_SCALE)
    print("plot_disk_server_traces: ended")
    return [dcc.Graph(id=fig_id, figure=fig)]


def plot_start_end(df, trace_name):
    if df is None:
        return
    print("plot_start_end: started")
    graphs = []

    def _plot_start_end(df, tag):
        df = compute_if_dask(df)
        fig = px.scatter(
            df,
            x=df.vdisk_started,
            y=df.vdisk_completed,
        )
        fig.update_layout(title=f"{trace_name} ({tag})", hovermode=False)
        fig_id = f"start_end_{tag}_{trace_name}"
        graphs.append(dcc.Graph(id=fig_id, figure=fig))
        if WRITE_FIGS_TO_DISK:
            fig_output_path = os.path.join(OUTPUT_PATH, fig_id)
            fig.write_image(f"{fig_output_path}.png", scale=FIG_SCALE)

    df_reads = df[df["is_read"] == 1]
    if len(df_reads) > 0:
        _plot_start_end(df_reads, "reads")
    df_writes = df[df["is_read"] == 0]
    if len(df_writes) > 0:
        _plot_start_end(df_writes, "writes")
    print("plot_start_end: ended")
    return graphs


def plot_load_lat(df, tag, trace_name):
    if df is None:
        return []
    print("plot_load_lat: started")
    fig_id = f"load_lat_{tag}_{trace_name}"
    graphs = []
    fig = px.line(
        df,
        x="mops",
        y=["lat_mean", "lat_median", "lat_p90", "lat_p99"],
    )
    fig.update_layout(
        title=f"{trace_name} ({tag}) MOPS",
        xaxis_title="MOPS",
        yaxis_title="Latency (us)",
        hovermode=False,
    )
    if WRITE_FIGS_TO_DISK:
        fig_output_path = os.path.join(OUTPUT_PATH, fig_id)
        fig.write_image(f"{fig_output_path}.png", scale=FIG_SCALE)
    graphs.append(dcc.Graph(id=fig_id, figure=fig))
    print("plot_load_lat: ended")
    return graphs
