from dash import Dash, html
from argparse import ArgumentParser
from data_processor import (
    get_controller_rw_isolation_traces,
    get_system_load_traces,
    get_disk_server_traces,
)
from plotter import (
    plot_controller_rw_isolation_traces,
    plot_system_load_traces,
    plot_disk_server_traces,
    plot_compare_weights,
    plot_iops,
    plot_iops_per_100ms,
    plot_start_end,
    plot_load_lat,
)
import constants as ct
from utils import cd


def get_args():
    parser = ArgumentParser(description="sandook: dashboard")
    parser.add_argument(
        "--input-path",
        type=str,
        default=ct.DEFAULT_TELEMETRY_ROOT,
        help="Path to input telemetry files",
    )
    parser.add_argument(
        "--no-weights",
        default=False,
        action="store_true",
        help="Do not plot comparative weights",
    )
    parser.add_argument(
        "--no-aggregates",
        default=False,
        action="store_true",
        help="Do not plot aggregate disk server stats (speeds up plotting)",
    )
    args = parser.parse_args()
    return args


def main(input_path, no_weights, no_aggregates):
    html_divs = []

    df_system_load = get_system_load_traces(input_path)
    if df_system_load is not None:
        html_divs.extend(plot_system_load_traces(df_system_load))

    df_controller_rw_isolation = get_controller_rw_isolation_traces(input_path)
    if df_controller_rw_isolation is not None:
        html_divs.extend(
            plot_controller_rw_isolation_traces(df_controller_rw_isolation)
        )

    dfs_disk_server = get_disk_server_traces(input_path, no_aggregates)
    if dfs_disk_server is not None:
        if not no_weights:
            html_divs.extend(plot_compare_weights(dfs_disk_server))
        for df, df_iops, df_iops_per_100ms, trace_name in dfs_disk_server:
            html_divs.extend(plot_disk_server_traces(df, trace_name))
            html_divs.extend(plot_iops(df_iops, trace_name))
            html_divs.extend(plot_iops_per_100ms(df_iops_per_100ms, trace_name))

    app = Dash(__name__)
    app.layout = html.Div(html_divs)
    app.run(debug=False, host="0.0.0.0")


if __name__ == "__main__":
    args = get_args()
    main(args.input_path, args.no_weights, args.no_aggregates)
