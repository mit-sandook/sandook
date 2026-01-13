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

# Output directory
OUTPUTS_DIR = "."

def get_latency_col(quantile: float) -> str:
    if quantile == 0.9:
        return kP90
    if quantile == 0.99:
        return kP99
    if quantile == 0.5:
        return kP50
    raise Exception(f"Unexpected quantile: {quantile}")

# Maximum latency to plot (ms)
MAX_LATENCY_MS = 1000
# Latency limit to plot (ms)
LATENCY_LIMIT_MS = 1.0
# Load limit to plot (MOPS)
LOAD_LIMIT_MOPS = None


kActual = "Actual"
kP50 = "Median"
kP90 = "90th"
kP99 = "99th"


def get_args():
    global args
    parser = ArgumentParser()
    parser.add_argument(
        "--baseline",
        type=str,
        required=True,
        help="NoOp RandomReadWrite",
    )
    parser.add_argument(
        "--rw",
        type=str,
        required=True,
        help="RWIsolationWeak RandomReadWrite",
    )
    parser.add_argument(
        "--sandook",
        type=str,
        required=True,
        help="ProfileGuidedRWIsolationWeak WeightedReadWrite CongestionControl",
    )
    parser.add_argument(
        "--output-filename",
        type=str,
        required=True,
        help="Name of output file (without extension)",
    )
    args = parser.parse_args()
    return args


def format_filename(s):
    valid_chars = "-_.() %s%s" % (string.ascii_letters, string.digits)
    filename = "".join(c for c in s if c in valid_chars)
    filename = filename.replace(" ", "_")
    return filename


def get_df(filename):
    return pd.read_csv(filename, skipinitialspace=True)


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


def plot_iops_bar(baseline_df, rw_df, sandook_df, output_filename, quantile, latency_col):
    """
    Bar chart: best achievable throughput for each configuration while keeping
    the given quantile latency under LATENCY_LIMIT_MS.
    """
    configs = [
        ("Static Rt.", baseline_df, "red"),
        ("R/W Seg.", rw_df, "blue"),
        ("Sandook", sandook_df, "green"),
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


def plot(baseline, rw, sandook, output_filename, quantile):
    def x_axis_fmt_(x):
        return x * 1e-6

    fig, ax = plt.subplots(figsize=(6,3))
    ax.xaxis.set_major_formatter(
        ticker.FuncFormatter(lambda x, pos: (f"%.1f" % (x_axis_fmt_(x))))
    )
    ax.plot(baseline[0], baseline[1], label="Static Rt.", color="red", marker="o")
    ax.plot(rw[0], rw[1], label="R/W Seg.", color="blue", marker="s")
    ax.plot(sandook[0], sandook[1], label="Sandook", color="green", marker="x")
    ax.set_ylim(0, LATENCY_LIMIT_MS)
    ax.set_xlim(0, LOAD_LIMIT_MOPS)
    ax.grid()
    ax.set_xlabel("Load (million RPS)")
    ylabel_tag = f"P{int(quantile*100)}"
    ax.set_ylabel(f"{ylabel_tag} Latency (ms)")
    ax.legend(loc="upper left", labelspacing=0.1)
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

    df_baseline = get_df(args.baseline)
    df_rw = get_df(args.rw)
    df_sandook = get_df(args.sandook)

    # Produce both p90 and p99 curves (and corresponding bar charts).
    for quantile in [0.90, 0.99]:
        latency_col = get_latency_col(quantile)

        # Drop rows after latency limit for the selected quantile (consistent with old behavior).
        df_b = df_baseline[df_baseline[latency_col] < (LATENCY_LIMIT_MS * 1e3)]
        df_r = df_rw[df_rw[latency_col] < (LATENCY_LIMIT_MS * 1e3)]
        df_s = df_sandook[df_sandook[latency_col] < (LATENCY_LIMIT_MS * 1e3)]

        baseline = get_load_lat_values(df_b, latency_col)
        rw = get_load_lat_values(df_r, latency_col)
        sandook = get_load_lat_values(df_s, latency_col)
        plot(baseline, rw, sandook, args.output_filename, quantile)
        plot_iops_bar(df_b, df_r, df_s, args.output_filename, quantile, latency_col)

if __name__ == "__main__":
    main()
