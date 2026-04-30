#!/bin/bash
# A/B the global LIPP-membership prefilter in HybridPGMLippAdv.
# COMPUTE NODE ONLY. No cmake, no compile.
#
# Prerequisite on the LOGIN NODE (same repo path on shared FS):
#   bash scripts/login_prefilter_ab_build.sh
# That script builds two binaries: build/benchmark.prefilter_{on,off}.
# This Slurm job just runs them against the mixed workloads.
#
# Usage from repo root:
#   sbatch milestone3_prefilter_ab.slurm
#
# Outputs:
#   results/prefilter_ab/on/*  results/prefilter_ab/off/*

#SBATCH --job-name=m3-prefilter-ab
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

echo "=== verify data + workload ops + pre-built A/B binaries ==="
bash ./scripts/check_milestone3_mix_prereqs.sh
if [[ ! -x build/benchmark.prefilter_on ]] || [[ ! -x build/benchmark.prefilter_off ]]; then
  echo "ERROR: missing pre-built A/B binaries. On the login node, run:"
  echo "  bash scripts/login_prefilter_ab_build.sh"
  exit 1
fi

echo "=== run prefilter A/B (no cmake, no compile) ==="
bash ./scripts/run_prefilter_ab_bench.sh

echo "=== done. Compare with: ==="
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' results/prefilter_ab/on/*_0.100000i_0m_mix_results_table.csv"
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' results/prefilter_ab/off/*_0.100000i_0m_mix_results_table.csv"
