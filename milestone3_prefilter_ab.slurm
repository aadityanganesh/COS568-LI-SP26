#!/bin/bash
# A/B the global LIPP-membership prefilter in HybridPGMLippAdv.
# Rebuilds twice (kEnablePrefilter = true / false) and runs the full mix each time.
# No dataset download. No internet needed for build.
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

echo "=== verify prereqs (data + workload ops files exist) ==="
bash ./scripts/check_milestone3_mix_prereqs.sh

echo "=== run_prefilter_ab (rebuilds twice) ==="
bash ./scripts/run_prefilter_ab.sh

echo "=== done. Compare with: ==="
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' results/prefilter_ab/on/*_0.100000i_0m_mix_results_table.csv"
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' results/prefilter_ab/off/*_0.100000i_0m_mix_results_table.csv"
