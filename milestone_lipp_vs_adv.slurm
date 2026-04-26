#!/bin/bash
# LIPP vs HybridPGMLippAdv only — one dataset (default: Facebook via run script).
# No download or generate. Builds benchmark on the node (no internet).
#
# Override dataset on the compute node:
#   sbatch --export=DATASET=books_100M_public_uint64 milestone_lipp_vs_adv.slurm
#
# Default (no --export):
#   DATASET=fb_100M_public_uint64
#
# Usage:
#   sbatch milestone_lipp_vs_adv.slurm

#SBATCH --job-name=lipp-adv
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

chmod +x scripts/*.sh 2>/dev/null || true

echo "=== build_benchmark (no network) ==="
bash ./scripts/build_benchmark.sh

if [[ ! -x build/benchmark ]]; then
  echo "ERROR: build/benchmark missing after build. On vis run once:"
  echo "  bash scripts/create_minimal_cmake.sh && rm -rf build && bash scripts/build_benchmark.sh"
  exit 1
fi

export DATASET="${DATASET:-fb_100M_public_uint64}"
echo "=== LIPP vs HybridPGMLippAdv | dataset=${DATASET} ==="
bash ./scripts/run_lipp_vs_adv.sh

echo "=== done ==="
