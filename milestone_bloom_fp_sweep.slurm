#!/bin/bash
# Bloom fp sweep: rebuild + run LIPP vs Adv for each fp (default 0.02, 0.01, 0.005).
# No dataset download. No internet for build.
#
# Optional:
#   sbatch --export=ALL,DATASET=books_100M_public_uint64 milestone_bloom_fp_sweep.slurm
#   sbatch --export=ALL,BLOOM_FP_SWEEP="0.01 0.005" milestone_bloom_fp_sweep.slurm
#
# Usage:
#   sbatch milestone_bloom_fp_sweep.slurm

#SBATCH --job-name=bloom-fp
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=32G
#SBATCH --time=24:00:00
#SBATCH --output=slurm-%j.out

set -eo pipefail

cd "${SLURM_SUBMIT_DIR}"

export PATH=$(echo "${PATH:-}" | tr ':' '\n' | grep -v '/\.pyenv/' | paste -sd: -)
export PATH="/usr/bin:/bin:/usr/local/bin:${PATH:-}"

module purge
module load gcc/11 boost/1.85.0

chmod +x scripts/*.sh 2>/dev/null || true

export DATASET="${DATASET:-fb_100M_public_uint64}"
export BLOOM_FP_SWEEP="${BLOOM_FP_SWEEP:-0.02 0.01 0.005}"

echo "=== Bloom fp sweep | DATASET=${DATASET} | FPS=${BLOOM_FP_SWEEP} ==="
bash ./scripts/run_bloom_fp_sweep.sh

echo "=== done ==="
