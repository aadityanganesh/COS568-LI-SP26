#!/bin/bash
# Milestone 2 — benchmarks only (Facebook: 90% and 10% insert mixes).
# No download, cmake, generate, or build.
#
# Prereqs on shared filesystem:
#   - data/fb_100M_public_uint64
#   - data/fb_100M_public_uint64_ops_2M_*_0.100000i_0m_mix and ...0.900000i_0m_mix
#   - build/benchmark
#
# Usage (from repo root):
#   sbatch milestone2_bench_only.slurm

#SBATCH --job-name=m2-bench
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=32G
#SBATCH --time=08:00:00
#SBATCH --output=slurm-%j.out

set -eo pipefail

cd "${SLURM_SUBMIT_DIR}"

export PATH=$(echo "${PATH:-}" | tr ':' '\n' | grep -v '/\.pyenv/' | paste -sd: -)
export PATH="/usr/bin:/bin:/usr/local/bin:${PATH:-}"

module purge
module load gcc/11 boost/1.85.0

if [[ ! -x build/benchmark ]]; then
  echo "ERROR: build/benchmark missing or not executable. Build on vis first:"
  echo "  bash scripts/create_minimal_cmake.sh && rm -rf build && bash scripts/build_benchmark.sh"
  exit 1
fi

chmod +x scripts/*.sh 2>/dev/null || true

echo "=== run_milestone2_fb (benchmark only, Facebook) ==="
bash ./scripts/run_milestone2_fb.sh

echo "=== done ==="
