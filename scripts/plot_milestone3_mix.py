#!/usr/bin/env python3
"""
Milestone 3: 12 bar charts (throughput + index size) × (10% insert mix, 90% insert mix) × (books, fb, osmc).

Reads results/*_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix_results_table.csv and
     ..._0.900000i_0m_mix_results_table.csv

For each index_name in {DynamicPGM, LIPP, HybridPGMLipp, HybridPGMLippAdv}, picks the row
with highest mean of the three mixed_throughput_mops* columns (README rule).
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

MIX_COLS = [
    "mixed_throughput_mops1",
    "mixed_throughput_mops2",
    "mixed_throughput_mops3",
]
INDEX_ORDER = ["DynamicPGM", "LIPP", "HybridPGMLipp", "HybridPGMLippAdv"]
COLORS = {
    "DynamicPGM": "#4477AA",
    "LIPP": "#EE6677",
    "HybridPGMLipp": "#228833",
    "HybridPGMLippAdv": "#CCBB44",
}

DATASETS = [
    ("books", "books_100M_public_uint64"),
    ("fb", "fb_100M_public_uint64"),
    ("osmc", "osmc_100M_public_uint64"),
]
MIXES = [
    ("10pct_insert", "0.100000i_0m_mix", "10% inserts (mixed)"),
    ("90pct_insert", "0.900000i_0m_mix", "90% inserts (mixed)"),
]


def best_row_per_index(df: pd.DataFrame) -> dict[str, tuple[float, float]]:
    """index_name -> (mean_throughput, index_size_bytes)."""
    out: dict[str, tuple[float, float]] = {}
    for name in INDEX_ORDER:
        sub = df[df["index_name"] == name]
        if sub.empty:
            continue
        means = sub[MIX_COLS].mean(axis=1)
        best_idx = means.idxmax()
        row = sub.loc[best_idx]
        out[name] = (float(means.loc[best_idx]), float(row["index_size_bytes"]))
    return out


def plot_bars(vals: dict[str, float], ylabel: str, title: str, out: Path) -> None:
    labels = [k for k in INDEX_ORDER if k in vals]
    ys = [vals[k] for k in labels]
    colors = [COLORS[k] for k in labels]
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    x = range(len(labels))
    ax.bar(x, ys, color=colors, width=0.52, edgecolor="black", linewidth=0.35)
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels, rotation=18, ha="right")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(axis="y", linestyle="--", alpha=0.35)
    fig.tight_layout()
    fig.savefig(out, dpi=200)
    plt.close(fig)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", type=Path, default=Path("results"))
    ap.add_argument("--out-dir", type=Path, default=Path("."))
    args = ap.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    for short, full in DATASETS:
        for tag, mix_suffix, mix_title in MIXES:
            path = args.results_dir / f"{full}_ops_2M_0.000000rq_0.500000nl_{mix_suffix}_results_table.csv"
            if not path.is_file():
                print("skip missing:", path)
                continue
            df = pd.read_csv(path)
            best = best_row_per_index(df)
            t = {k: best[k][0] for k in best}
            s = {k: best[k][1] for k in best}
            tp = args.out_dir / f"milestone3_{short}_{tag}_throughput.png"
            sp = args.out_dir / f"milestone3_{short}_{tag}_index_size.png"
            plot_bars(
                t,
                "Mixed throughput (Mops/s)",
                f"{short.upper()} — {mix_title}",
                tp,
            )
            plot_bars(
                s,
                "Index size (bytes)",
                f"{short.upper()} — index size — {mix_title}",
                sp,
            )
            print("wrote", tp, sp)


if __name__ == "__main__":
    main()
