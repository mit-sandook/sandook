import os
import numpy as np
import string
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from argparse import ArgumentParser


plt.rcParams.update({
    "font.size": 14,
    "grid.linestyle": "dashed",
    "grid.linewidth": 0.5,
    "grid.alpha": 0.4,
})

QUANTILE = None

OUTPUTS_DIR = "."

MAX_LATENCY_MS = 1000

IOPS_LIMIT = 1.0 * 1e6
LATENCY_LIMIT_MS = 1


kActual = "Actual"
kP50 = "Median"
kP90 = "90th"
kP99 = "99th"


def get_latency_col(quantile: float) -> str:
    if quantile == 0.9:
        return kP90
    if quantile == 0.99:
        return kP99
    if quantile == 0.5:
        return kP50
    raise Exception(f"Unexpected quantile: {quantile}")


def get_args():
    global QUANTILE
    parser = ArgumentParser()
    parser.add_argument(
        "--baseline-traces",
        type=str,
        required=True,
        help="Baseline traces",
    )
    parser.add_argument(
        "--sandook-traces",
        type=str,
        required=True,
        help="Sandook traces",
    )
    parser.add_argument(
        "--show-legend",
        type=int,
        required=False,
        default=0,
        help="Show the legend",
    )
    parser.add_argument(
        "--output-filename",
        type=str,
        required=True,
        help="Name of output file (without extension)",
    )
    parser.add_argument(
        "--latency-quantile",
        type=float,
        required=False,
        default=0.9,
        help="Latency percentile (kept for backward-compat; script now plots both p90 and p99).",
    )
    parser.add_argument(
        "--optimal-mops",
        type=float,
        required=False,
        default=0,
        help="Optimal MOPS",
    )
    args = parser.parse_args()
    QUANTILE = args.latency_quantile
    return args


def format_filename(s):
    valid_chars = "-_.() %s%s" % (string.ascii_letters, string.digits)
    filename = "".join(c for c in s if c in valid_chars)
    filename = filename.replace(" ", "_")
    return filename


def get_df(filename):
    df = pd.read_csv(filename, skipinitialspace=True)
    return df


def get_dfs(filenames):
    return [(tag, get_df(filename)) for filename, tag in filenames.items()]


def drop_rows_after_saturation(df):
    """
    Compute the difference in the Actual column from previous row.
    The first occurence where this becomes negative (i.e., Actual starts to
    decrease) we drop all the following rows.
    This is essentially dropping all rows after the point where we see a fall
    in the offered load (throughput) to the server (indicating saturation).
    """
    # Avoid pandas SettingWithCopyWarning: operate on an owned copy and use .loc.
    df = df.copy()
    df.loc[:, 'Diff'] = df['Actual'] - df['Actual'].shift(1)
    df.loc[:, 'Diff'] = df['Diff'].fillna(0)
    try:
        ix = df[df['Diff'] < 0].iloc[0].name
        ix = int(ix)
        if ix > 0:
            df = df.iloc[:ix]
            print(f'Dropped all rows after: {ix}')
    except BaseException:
        return df
    return df


def get_load_lat_values(df, col):
    df = drop_rows_after_saturation(df)
    vals = []
    lat_scale = 1e-3
    x = df[kActual]
    y = df[col] * lat_scale
    if len(df) != 0:
        x = x._append(pd.Series(x.iloc[-1]))
        y = y._append(pd.Series(MAX_LATENCY_MS))
    return [x, y]

def get_best_iops_under_latency_ms(df, latency_ms, latency_col):
    """
    Return the maximum throughput (IOPS/RPS) achievable while staying under the
    given latency bound for the specified quantile column.
    """
    df = drop_rows_after_saturation(df)
    if len(df) == 0:
        return 0.0
    df = df[df[latency_col] < (latency_ms * 1e3)]
    if len(df) == 0:
        return 0.0
    return float(df[kActual].max())

def plot_iops_bar(baseline_df, sandook_df, output_filename, optimal_mops, quantile, latency_col):
    """
    Bar chart: best achievable throughput for each configuration while keeping
    the given quantile latency under LATENCY_LIMIT_MS.
    """
    configs = [
        ("Sandook", sandook_df, "green"),
        ("Static Rt.", baseline_df, "red"),
    ]

    best_iops = [
        get_best_iops_under_latency_ms(df, LATENCY_LIMIT_MS, latency_col) for _, df, _ in configs
    ]
    labels = [name for name, _, _ in configs]
    colors = [c for _, _, c in configs]

    fig, ax = plt.subplots(figsize=(6, 3))
    x = np.arange(len(labels))
    bars = ax.bar(x, best_iops, color=colors, alpha=0.9)

    def iops_fmt_(v):
        return v * 1e-6

    ax.yaxis.set_major_formatter(
        ticker.FuncFormatter(lambda v, pos: (f"%.1f" % (iops_fmt_(v))))
    )
    ax.set_xticks(x, labels)
    ax.set_ylabel("IOPS (million)")
    ax.set_ylim(0, max(best_iops) * 1.15 if max(best_iops) > 0 else 1.0)
    ax.grid(axis="y")

    for bar, v in zip(bars, best_iops):
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            bar.get_height(),
            f"{iops_fmt_(v):.2f}",
            ha="center",
            va="bottom",
            fontsize=10,
        )

    # Optional "ideal" marker if provided.
    if optimal_mops != 0:
        ideal_iops = optimal_mops * 1e6
        ax.axhline(ideal_iops, linewidth=2, color="black", linestyle="--", label="Ideal")
        ax.legend(loc="upper right")

    plt.tight_layout()

    ylabel_tag = f"P{int(quantile*100)}"
    out = f"{output_filename}-{ylabel_tag}-iops"
    pdf_filepath = os.path.join(OUTPUTS_DIR, f"{out}.pdf")
    png_filepath = os.path.join(OUTPUTS_DIR, f"{out}.png")
    for filepath in [pdf_filepath, png_filepath]:
        if os.path.exists(filepath):
            os.remove(filepath)
    fig.savefig(pdf_filepath)
    fig.savefig(png_filepath, dpi=500)
    plt.close(fig)


def plot(baseline, sandook, output_filename, optimal_mops, show_legend, quantile):
    def x_axis_fmt_(x):
        return x * 1e-6

    fig, ax = plt.subplots(figsize=(6,3))
    ax.xaxis.set_major_formatter(
        ticker.FuncFormatter(lambda x, pos: (f"%.1f" % (x_axis_fmt_(x))))
    )
    ax.plot(sandook[0], sandook[1], label="Sandook", color="green", marker="x")
    ax.plot(baseline[0], baseline[1], label="Static Rt.", color="red", marker="o")
    if optimal_mops != 0:
        plt.axvline(optimal_mops * 1e6, linewidth=2, color="black", label="Ideal", linestyle="--")
    ax.set_ylim(0, LATENCY_LIMIT_MS)
    max_x = max(float(baseline[0].max()), float(sandook[0].max())) if len(baseline[0]) and len(sandook[0]) else 0
    if max_x > 0:
        ax.set_xlim(0, max_x * 1.05)
    ax.grid()
    ax.set_xlabel("Load (million RPS)")
    ylabel_tag = f"P{int(quantile*100)}"
    ax.set_ylabel(f"{ylabel_tag} Latency (ms)")
    if show_legend:
        ax.legend()
    plt.tight_layout()

    out = f"{output_filename}-{ylabel_tag}"
    pdf_filepath = os.path.join(OUTPUTS_DIR, f"{out}.pdf")
    png_filepath = os.path.join(OUTPUTS_DIR, f"{out}.png")
    filepaths = [pdf_filepath, png_filepath]
    for filepath in filepaths:
        if os.path.exists(filepath):
            os.remove(filepath)
    fig.savefig(pdf_filepath)
    fig.savefig(png_filepath, dpi=500)
    plt.close(fig)


def main():
    args = get_args()

    df_baseline = get_df(args.baseline_traces)
    df_sandook = get_df(args.sandook_traces)

    # Produce both p90 and p99 curves (and corresponding bar charts).
    for quantile in [0.90, 0.99]:
        latency_col = get_latency_col(quantile)
        baseline = get_load_lat_values(df_baseline, latency_col)
        sandook = get_load_lat_values(df_sandook, latency_col)
        plot(baseline, sandook, args.output_filename, args.optimal_mops, args.show_legend > 0, quantile)
        plot_iops_bar(df_baseline, df_sandook, args.output_filename, args.optimal_mops, quantile, latency_col)

if __name__ == "__main__":
    main()
