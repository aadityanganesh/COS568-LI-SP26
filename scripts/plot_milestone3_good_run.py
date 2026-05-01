#!/usr/bin/env python3
"""
Generate 12 bar charts from the "good" Stage 5 benchmark run (Adroit, -r 3).

  6 plots for 10% insert / 90% lookup (lookup-heavy)
  6 plots for 90% insert / 10% lookup (insert-heavy)

Per mix × dataset:
  - Throughput (median of 3 repeats): LIPP, HybridPGMLippAdv, DynamicPGM
  - Index size (bytes from CSV): same three indexes

One representative PGM/search variant per index type (best median throughput
for DPGM and Adv on that dataset in that run). See GOOD_RUN_META below.

Output: docs/milestone3_good_run/*.png and tables_good_run.md (tracked in git).

Requires: pip install matplotlib
"""
from __future__ import annotations

import os
import sys

# --- "Good run" medians (median of mixed_throughput_mops1..3) and index sizes ---
# Sizes are bytes from the benchmark CSV. Throughput in M ops/s.
GOOD_RUN = {
    "10pct_insert": {
        "books": {
            "LIPP": {"mops": 5.95624, "size": 11_818_525_760},
            "HybridPGMLippAdv": {
                "mops": 5.26743,
                "size": 11_816_089_464,
                "variant": "InterpolationSearch, ε=256",
            },
            "DynamicPGM": {
                "mops": 0.908218,
                "size": 1_703_429_848,
                "variant": "LinearSearch, ε=32",
            },
        },
        "fb": {
            "LIPP": {"mops": 6.06331, "size": 12_700_946_864},
            "HybridPGMLippAdv": {
                "mops": 5.36248,
                "size": 12_692_726_328,
                "variant": "BinarySearch, ε=64",
            },
            "DynamicPGM": {
                "mops": 0.777375,
                "size": 1_705_226_028,
                "variant": "BinarySearch, ε=64",
            },
        },
        "osmc": {
            "LIPP": {"mops": 3.34300, "size": 20_602_887_232},
            "HybridPGMLippAdv": {
                "mops": 3.19990,
                "size": 20_728_431_784,
                "variant": "BinarySearch, ε=128",
            },
            "DynamicPGM": {
                "mops": 0.875360,
                "size": 1_701_834_168,
                "variant": "BinarySearch, ε=128",
            },
        },
    },
    "90pct_insert": {
        "books": {
            "LIPP": {"mops": 3.04890, "size": 11_753_613_200},
            "HybridPGMLippAdv": {
                "mops": 3.86606,
                "size": 11_691_092_636,
                "variant": "InterpolationSearch, ε=256",
            },
            "DynamicPGM": {
                "mops": 2.71130,
                "size": 1_700_011_168,
                "variant": "InterpolationSearch, ε=256",
            },
        },
        "fb": {
            "LIPP": {"mops": 2.61839, "size": 12_656_662_928},
            "HybridPGMLippAdv": {
                "mops": 3.90901,
                "size": 12_543_284_404,
                "variant": "BinarySearch, ε=64",
            },
            "DynamicPGM": {
                "mops": 2.79756,
                "size": 1_705_154_448,
                "variant": "BinarySearch, ε=64",
            },
        },
        "osmc": {
            "LIPP": {"mops": 2.00322, "size": 20_408_967_088},
            "HybridPGMLippAdv": {
                "mops": 3.26981,
                "size": 20_291_323_864,
                "variant": "BinarySearch, ε=128",
            },
            "DynamicPGM": {
                "mops": 2.51666,
                "size": 1_701_839_508,
                "variant": "BinarySearch, ε=128",
            },
        },
    },
}

LABELS = {
    "LIPP": "LIPP",
    "HybridPGMLippAdv": "HybridPGMLippAdv",
    "DynamicPGM": "DynamicPGM",
}

COLORS = {
    "LIPP": "#2ecc71",
    "HybridPGMLippAdv": "#3498db",
    "DynamicPGM": "#e74c3c",
}

ORDER = ["LIPP", "HybridPGMLippAdv", "DynamicPGM"]

DATASET_TITLES = {
    "books": "books (100M uint64)",
    "fb": "Facebook (100M uint64)",
    "osmc": "OSMC (100M uint64)",
}


def bytes_to_gb(b: float) -> float:
    return b / 1.0e9


def main() -> int:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("Install matplotlib:  pip install matplotlib", file=sys.stderr)
        return 1

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    # Under docs/ so PNGs are not dropped by root .gitignore (*.png).
    out_dir = os.path.join(root, "docs", "milestone3_good_run")
    os.makedirs(out_dir, exist_ok=True)

    mix_keys = [
        ("10pct_insert", "10% insert / 90% lookup", "10pct_insert"),
        ("90pct_insert", "90% insert / 10% lookup", "90pct_insert"),
    ]

    for mix_key, mix_title, fname_prefix in mix_keys:
        block = GOOD_RUN[mix_key]
        for ds in ("books", "fb", "osmc"):
            data = block[ds]
            # Throughput
            fig, ax = plt.subplots(figsize=(7.5, 4.5))
            names = [LABELS[k] for k in ORDER]
            vals = [data[k]["mops"] for k in ORDER]
            colors = [COLORS[k] for k in ORDER]
            bars = ax.bar(names, vals, color=colors, edgecolor="#333", linewidth=0.8)
            ax.set_ylabel("Throughput (M ops/s)")
            ax.set_title(
                f"{DATASET_TITLES[ds]}\n{mix_title}\n(median of 3 repeats; PGM variant in caption)"
            )
            ax.set_ylim(0, max(vals) * 1.15)
            for b, v in zip(bars, vals):
                ax.text(
                    b.get_x() + b.get_width() / 2,
                    v + max(vals) * 0.02,
                    f"{v:.2f}",
                    ha="center",
                    va="bottom",
                    fontsize=10,
                )
            foot = (
                f"DPGM: {data['DynamicPGM'].get('variant', '')}  |  "
                f"Adv: {data['HybridPGMLippAdv'].get('variant', '')}"
            )
            fig.text(0.5, 0.02, foot, ha="center", fontsize=8, style="italic")
            plt.tight_layout(rect=(0, 0.08, 1, 1))
            tp_path = os.path.join(
                out_dir, f"{fname_prefix}_{ds}_throughput.png"
            )
            fig.savefig(tp_path, dpi=150)
            plt.close(fig)

            # Index size
            fig2, ax2 = plt.subplots(figsize=(7.5, 4.5))
            sizes_gb = [bytes_to_gb(data[k]["size"]) for k in ORDER]
            bars2 = ax2.bar(names, sizes_gb, color=colors, edgecolor="#333", linewidth=0.8)
            ax2.set_ylabel("Index size (GB)")
            ax2.set_title(f"{DATASET_TITLES[ds]}\n{mix_title}\nindex size (reported bytes / 10⁹)")
            ax2.set_ylim(0, max(sizes_gb) * 1.12)
            for b, v in zip(bars2, sizes_gb):
                ax2.text(
                    b.get_x() + b.get_width() / 2,
                    v + max(sizes_gb) * 0.015,
                    f"{v:.2f}",
                    ha="center",
                    va="bottom",
                    fontsize=10,
                )
            plt.tight_layout()
            sz_path = os.path.join(out_dir, f"{fname_prefix}_{ds}_index_size.png")
            fig2.savefig(sz_path, dpi=150)
            plt.close(fig2)

    # Markdown summary table
    md_path = os.path.join(out_dir, "tables_good_run.md")
    lines = [
        "# Milestone 3 good run — summary tables",
        "",
        "Source: Stage 5 benchmark on Adroit (`-r 3`). Throughput = **median** of the three mixed-throughput columns per row.",
        "",
        "## 10% insert / 90% lookup",
        "",
        "| Dataset | LIPP Mops/s | Adv Mops/s | DPGM Mops/s | LIPP GB | Adv GB | DPGM GB |",
        "|---------|------------:|-----------:|------------:|--------:|-------:|--------:|",
    ]
    for ds in ("books", "fb", "osmc"):
        d = GOOD_RUN["10pct_insert"][ds]
        lines.append(
            f"| {ds} | {d['LIPP']['mops']:.3f} | {d['HybridPGMLippAdv']['mops']:.3f} | {d['DynamicPGM']['mops']:.3f} | "
            f"{bytes_to_gb(d['LIPP']['size']):.2f} | {bytes_to_gb(d['HybridPGMLippAdv']['size']):.2f} | {bytes_to_gb(d['DynamicPGM']['size']):.2f} |"
        )
    lines.extend(
        [
            "",
            "## 90% insert / 10% lookup",
            "",
            "| Dataset | LIPP Mops/s | Adv Mops/s | DPGM Mops/s | LIPP GB | Adv GB | DPGM GB |",
            "|---------|------------:|-----------:|------------:|--------:|-------:|--------:|",
        ]
    )
    for ds in ("books", "fb", "osmc"):
        d = GOOD_RUN["90pct_insert"][ds]
        lines.append(
            f"| {ds} | {d['LIPP']['mops']:.3f} | {d['HybridPGMLippAdv']['mops']:.3f} | {d['DynamicPGM']['mops']:.3f} | "
            f"{bytes_to_gb(d['LIPP']['size']):.2f} | {bytes_to_gb(d['HybridPGMLippAdv']['size']):.2f} | {bytes_to_gb(d['DynamicPGM']['size']):.2f} |"
        )
    lines.append("")
    with open(md_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"Wrote 12 PNGs and {md_path}")
    for f in sorted(os.listdir(out_dir)):
        if f.endswith(".png"):
            print(f"  {os.path.join(out_dir, f)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
