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
MODULE_PATH="$PATH"

# Resolve cmake on the compute node, same logic as scripts/login_milestone3_prep.sh.
# Necessary because run_prefilter_ab.sh rebuilds twice from inside this Slurm job.
if [[ -x /usr/bin/cmake ]]; then
  export PATH="/usr/bin:${MODULE_PATH}"
else
  if ! module load anaconda3/2023.3; then
    echo "ERROR: module load anaconda3/2023.3 failed."
    exit 1
  fi
  ANACONDA_ROOT="${CONDA_PREFIX:-/usr/licensed/anaconda3/2023.3}"
  if [[ -f "${ANACONDA_ROOT}/etc/profile.d/conda.sh" ]]; then
    # shellcheck source=/dev/null
    source "${ANACONDA_ROOT}/etc/profile.d/conda.sh"
  fi
  COS568_CMAKE=""
  if conda activate cos568 2>/dev/null; then
    if [[ -x "${CONDA_PREFIX}/bin/cmake" ]]; then
      COS568_CMAKE="${CONDA_PREFIX}/bin/cmake"
    fi
  fi
  if [[ -z "${COS568_CMAKE}" ]]; then
    for ENV_ROOT in "${HOME}/.conda/envs/cos568" "${ANACONDA_ROOT}/envs/cos568"; do
      if [[ -x "${ENV_ROOT}/bin/cmake" ]]; then
        COS568_CMAKE="${ENV_ROOT}/bin/cmake"
        break
      fi
    done
  fi
  if [[ -z "${COS568_CMAKE}" ]] || [[ ! -x "${COS568_CMAKE}" ]]; then
    echo "ERROR: No usable cmake found on compute node."
    conda env list 2>&1 || true
    exit 1
  fi
  export PATH="$(dirname "${COS568_CMAKE}"):${MODULE_PATH}"
fi

echo "=== cmake / gcc check ==="
command -v cmake
command -v gcc

chmod +x scripts/*.sh 2>/dev/null || true

echo "=== verify prereqs (data + workload ops files exist) ==="
bash ./scripts/check_milestone3_mix_prereqs.sh

echo "=== run_prefilter_ab (rebuilds twice) ==="
bash ./scripts/run_prefilter_ab.sh

echo "=== done. Compare with: ==="
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' results/prefilter_ab/on/*_0.100000i_0m_mix_results_table.csv"
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' results/prefilter_ab/off/*_0.100000i_0m_mix_results_table.csv"
