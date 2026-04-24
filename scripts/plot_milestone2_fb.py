#!/usr/bin/env python3
"""
Milestone 2 (Facebook only): four bar plots — throughput × 2 workloads, index size × 2 workloads.

Reads:
  results/fb_100M_public_uint64_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix_results_table.csv
  results/fb_100M_public_uint64_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix_results_table.csv

If the 0.1-mix CSV has two appended runs (14+ data rows), use --use-last-block-only (default)
to keep only the last 7 rows (your “Block B” after the asm fix), matching the README pick-best-row rule.
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
INDEX_ORDER = ["DynamicPGM", "LIPP", "HybridPGMLipp"]
COLORS = {"DynamicPGM": "#4477AA", "LIPP": "#EE6677", "HybridPGMLipp": "#228833"}


def _mean_throughput(row: pd.Series) -> float:
    return float(row[MIX_COLS].mean())


def best_row_per_index(df: pd.DataFrame) -> pd.DataFrame:
    """One row per index_name with highest mean mixed throughput."""
    rows = []
    for name in INDEX_ORDER:
        sub = df[df["index_name"] == name]
        if sub.empty:
            continue
        means = sub.apply(_mean_throughput, axis=1)
        best = sub.loc[means.idxmax()]
        rows.append(best)
    return pd.DataFrame(rows)


def load_mix_csv(path: Path, use_last_block_only: bool, rows_per_block: int) -> pd.DataFrame:
    df = pd.read_csv(path)
    if use_last_block_only and len(df) >= 2 * rows_per_block:
        df = df.iloc[-rows_per_block:].copy()
    return df


def plot_bars(
    values: dict[str, float],
    ylabel: str,
    title: str,
    out_path: Path,
) -> None:
    labels = [k for k in INDEX_ORDER if k in values]
    ys = [values[k] for k in labels]
    colors = [COLORS[k] for k in labels]

    fig, ax = plt.subplots(figsize=(6.5, 4.2))
    x = range(len(labels))
    ax.bar(x, ys, color=colors, width=0.55, edgecolor="black", linewidth=0.4)
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels, rotation=12, ha="right")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(axis="y", linestyle="--", alpha=0.35)
    fig.tight_layout()
    fig.savefig(out_path, dpi=200)
    plt.close(fig)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument(
        "--results-dir",
        type=Path,
        default=Path("results"),
        help="Directory containing Milestone 2 FB mix CSVs",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=Path("."),
        help="Where to write PNG files",
    )
    p.add_argument(
        "--use-last-block-only",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="For 0.1 mix file: if >=14 data rows, keep only last 7 (Block B)",
    )
    p.add_argument(
        "--rows-per-block",
        type=int,
        default=7,
        help="Data rows per experiment block (3 PGM + 1 LIPP + 3 Hybrid)",
    )
    args = p.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    base = "fb_100M_public_uint64_ops_2M_0.000000rq_0.500000nl"
    path_01 = args.results_dir / f"{base}_0.100000i_0m_mix_results_table.csv"
    path_09 = args.results_dir / f"{base}_0.900000i_0m_mix_results_table.csv"

    for path in (path_01, path_09):
        if not path.is_file():
            raise SystemExit(f"Missing CSV: {path}")

    df01 = load_mix_csv(path_01, args.use_last_block_only, args.rows_per_block)
    df09 = load_mix_csv(path_09, use_last_block_only=False, rows_per_block=args.rows_per_block)

    best01 = best_row_per_index(df01)
    best09 = best_row_per_index(df09)

    def throughput_map(b: pd.DataFrame) -> dict[str, float]:
        m: dict[str, float] = {}
        for _, row in b.iterrows():
            m[str(row["index_name"])] = _mean_throughput(row)
        return m

    def size_map(b: pd.DataFrame) -> dict[str, float]:
        m: dict[str, float] = {}
        for _, row in b.iterrows():
            m[str(row["index_name"])] = float(row["index_size_bytes"])
        return m

    t01, t09 = throughput_map(best01), throughput_map(best09)
    s01, s09 = size_map(best01), size_map(best09)

    plot_bars(
        t01,
        "Mixed throughput (Mops/s)",
        "Facebook — mixed workload (10% inserts, 0.5 neg. lookup)",
        args.out_dir / "milestone2_fb_throughput_0.1mix.png",
    )
    plot_bars(
        t09,
        "Mixed throughput (Mops/s)",
        "Facebook — mixed workload (90% inserts, 0.5 neg. lookup)",
        args.out_dir / "milestone2_fb_throughput_0.9mix.png",
    )
    plot_bars(
        s01,
        "Index size (bytes)",
        "Facebook — index size @ 10% insert mix (best row each index)",
        args.out_dir / "milestone2_fb_index_size_0.1mix.png",
    )
    plot_bars(
        s09,
        "Index size (bytes)",
        "Facebook — index size @ 90% insert mix (best row each index)",
        args.out_dir / "milestone2_fb_index_size_0.9mix.png",
    )

    print("Wrote:")
    for name in (
        "milestone2_fb_throughput_0.1mix.png",
        "milestone2_fb_throughput_0.9mix.png",
        "milestone2_fb_index_size_0.1mix.png",
        "milestone2_fb_index_size_0.9mix.png",
    ):
        print(f"  {args.out_dir / name}")


if __name__ == "__main__":
    main()
