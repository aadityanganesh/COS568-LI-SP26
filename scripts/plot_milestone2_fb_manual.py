#!/usr/bin/env python3
"""Fixed Facebook Milestone 2 numbers (0.1 mix; single run @ 0.9 mix)."""

from pathlib import Path

import matplotlib.pyplot as plt

INDEX_ORDER = ["DynamicPGM", "LIPP", "HybridPGMLipp"]
COLORS = {"DynamicPGM": "#4477AA", "LIPP": "#EE6677", "HybridPGMLipp": "#228833"}

# Throughput (Mops/s), index size (bytes) — best row per index, means of 3 runs
DATA_01 = {
    "DynamicPGM": (0.792481667, 1_702_558_388),
    "LIPP": (5.116733333, 12_700_946_864),
    "HybridPGMLipp": (0.916954, 12_687_700_704),
}
DATA_09 = {
    "DynamicPGM": (2.646136667, 1_702_518_268),
    "LIPP": (1.238575333, 12_656_662_928),
    "HybridPGMLipp": (3.217313333, 12_538_249_840),
}


def bar_plot(values: dict[str, float], ylabel: str, title: str, path: Path) -> None:
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
    fig.savefig(path, dpi=200)
    plt.close(fig)


def main() -> None:
    out = Path(__file__).resolve().parents[1]
    t01 = {k: DATA_01[k][0] for k in INDEX_ORDER}
    t09 = {k: DATA_09[k][0] for k in INDEX_ORDER}
    s01 = {k: DATA_01[k][1] for k in INDEX_ORDER}
    s09 = {k: DATA_09[k][1] for k in INDEX_ORDER}

    bar_plot(
        t01,
        "Mixed throughput (Mops/s)",
        "Facebook — 10% inserts, 0.5 neg. lookup",
        out / "milestone2_fb_throughput_0.1mix.png",
    )
    bar_plot(
        t09,
        "Mixed throughput (Mops/s)",
        "Facebook — 90% inserts, 0.5 neg. lookup",
        out / "milestone2_fb_throughput_0.9mix.png",
    )
    bar_plot(
        s01,
        "Index size (bytes)",
        "Facebook — index size @ 10% insert mix",
        out / "milestone2_fb_index_size_0.1mix.png",
    )
    bar_plot(
        s09,
        "Index size (bytes)",
        "Facebook — index size @ 90% insert mix",
        out / "milestone2_fb_index_size_0.9mix.png",
    )
    print("Wrote 4 PNGs in", out)


if __name__ == "__main__":
    main()
