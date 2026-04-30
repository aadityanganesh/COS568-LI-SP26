#!/bin/bash
# Milestone 3 — benchmarks only on COMPUTE (no download, generate, or compile).
#
# Build on the LOGIN NODE first (same checkout path on shared FS):
#   bash scripts/login_milestone3_prep.sh build-only
# Use full prep if data or workload ops files are missing:
#   bash scripts/login_milestone3_prep.sh
#
# Usage (from repo root): sbatch milestone3_bench_only.slurm

#SBATCH --job-name=m3-bench
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=32G
#SBATCH --time=16:00:00
#SBATCH --output=slurm-%j.out

set -eo pipefail

cd "${SLURM_SUBMIT_DIR}"

export PATH=$(echo "${PATH:-}" | tr ':' '\n' | grep -v '/\.pyenv/' | paste -sd: -)
export PATH="/usr/bin:/bin:/usr/local/bin:${PATH:-}"

module purge
module load gcc/11 boost/1.85.0

chmod +x scripts/*.sh 2>/dev/null || true

echo "=== verify prereqs (built on login node) ==="
bash ./scripts/check_milestone3_mix_prereqs.sh

echo "=== run_milestone3_mix (evaluation only) ==="
bash ./scripts/run_milestone3_mix.sh

echo "=== done. Plot: python scripts/plot_milestone3_mix.py ==="
