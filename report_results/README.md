# Report results (Milestone 3)

This folder holds **figures and tables** for the Milestone 3 writeup, generated from the Stage 5 Adroit benchmark run.

## Contents

| Path | Description |
|------|-------------|
| `milestone3_good_run/` | 12 PNG bar charts + `tables_good_run.md` (12 markdown tables, one per figure) |
| `plot_milestone3_good_run.py` | Regenerates those files (requires `matplotlib`) |

## Regenerate plots

From the **repository root**:

```bash
pip install matplotlib
python report_results/plot_milestone3_good_run.py
```

Outputs are written to `report_results/milestone3_good_run/`.

## Chart naming

- `10pct_insert_*` — 10% insert / 90% lookup (lookup-heavy)
- `90pct_insert_*` — 90% insert / 10% lookup (insert-heavy)
- `*_throughput.png` — LIPP vs HybridPGMLippAdv vs DynamicPGM (M ops/s)
- `*_index_size.png` — same three, index size in GB
