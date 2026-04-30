#!/bin/bash
# Milestone 3 — COMPUTE NODE ONLY (sbatch)
#
# Prerequisite on the LOGIN NODE (same repo path on shared FS):
#   bash scripts/login_milestone3_prep.sh
# That step downloads data (if needed), writes minimal CMakeLists, generates
# workloads, and builds build/benchmark. This job only runs evaluation.
#
# Books + FB + OSMC mixed workloads (10% and 90% insert):
#   DynamicPGM, LIPP, HybridPGMLipp (naive), HybridPGMLippAdv.
#
# Usage: sbatch milestone3.slurm

#SBATCH --job-name=milestone3
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

echo "=== Milestone 3 benchmark finished. Plot with: python scripts/plot_milestone3_mix.py ==="
