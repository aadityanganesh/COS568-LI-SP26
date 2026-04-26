#!/bin/bash
# Milestone 3 — benchmarks only (no download, generate, or full pipeline).
# Builds benchmark on the compute node (cmake+make; no internet).
#
# Prereqs on shared filesystem:
#   - Minimal CMakeLists from a prior: bash scripts/create_minimal_cmake.sh (once on vis if needed)
#   - data/books_100M_public_uint64, fb_..., osmc_...
#   - data/*_ops_2M_* mix files from scripts/generate_workloads.sh
#
# Usage (from repo root):
#   sbatch milestone3_bench_only.slurm

#SBATCH --job-name=m3-bench
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
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

echo "=== build_benchmark (no network) ==="
bash ./scripts/build_benchmark.sh

if [[ ! -x build/benchmark ]]; then
  echo "ERROR: build/benchmark missing after build. On vis run once:"
  echo "  bash scripts/create_minimal_cmake.sh && rm -rf build && bash scripts/build_benchmark.sh"
  exit 1
fi

echo "=== run_milestone3_mix (benchmark only) ==="
bash ./scripts/run_milestone3_mix.sh

echo "=== done. Plot: python scripts/plot_milestone3_mix.py ==="
