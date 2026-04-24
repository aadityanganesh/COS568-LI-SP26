#!/bin/bash
# Milestone 3 — RUN ON COMPUTE NODE (sbatch)
#
# Books + FB + OSMC mixed workloads (10% and 90% insert):
#   DynamicPGM, LIPP, HybridPGMLipp (naive), HybridPGMLippAdv (sorted flush).
#
# Usage: sbatch milestone3.slurm

#SBATCH --job-name=milestone3
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

echo "=== STEP: load gcc + boost ==="
module load gcc/11 boost/1.85.0
MODULE_PATH="$PATH"

echo "=== STEP: load anaconda ==="
if ! module load anaconda3/2023.3; then
  echo "ERROR: module load anaconda3/2023.3 failed."
  exit 1
fi

ANACONDA_ROOT="${CONDA_PREFIX:-/usr/licensed/anaconda3/2023.3}"
CONDA_EXE="${ANACONDA_ROOT}/bin/conda"

if [ ! -f "${ANACONDA_ROOT}/etc/profile.d/conda.sh" ]; then
  echo "ERROR: missing ${ANACONDA_ROOT}/etc/profile.d/conda.sh"
  exit 1
fi

# shellcheck source=/dev/null
source "${ANACONDA_ROOT}/etc/profile.d/conda.sh"

if [ -x "${CONDA_EXE}" ]; then
  eval "$("${CONDA_EXE}" shell.bash hook 2>/dev/null)" || true
fi

echo "=== STEP: select cmake ==="
if [ -x /usr/bin/cmake ]; then
  export PATH="/usr/bin:${MODULE_PATH}"
else
  COS568_CMAKE=""
  if conda activate cos568; then
    if [ -x "${CONDA_PREFIX}/bin/cmake" ]; then
      COS568_CMAKE="${CONDA_PREFIX}/bin/cmake"
    fi
  fi
  if [ -z "${COS568_CMAKE}" ]; then
    for ENV_ROOT in "${HOME}/.conda/envs/cos568" "${ANACONDA_ROOT}/envs/cos568"; do
      if [ -x "${ENV_ROOT}/bin/cmake" ]; then
        COS568_CMAKE="${ENV_ROOT}/bin/cmake"
        export CONDA_PREFIX="${ENV_ROOT}"
        break
      fi
    done
  fi
  if [ -z "${COS568_CMAKE}" ] || [ ! -x "${COS568_CMAKE}" ]; then
    echo "ERROR: No usable cmake found."
    conda env list 2>&1 || true
    exit 1
  fi
  export PATH="$(dirname "${COS568_CMAKE}"):${MODULE_PATH}"
fi

echo "=== cmake / gcc check ==="
command -v cmake
command -v gcc

chmod +x scripts/*.sh

need_data() {
  [[ -f data/books_100M_public_uint64 ]] && [[ -f data/fb_100M_public_uint64 ]] && [[ -f data/osmc_100M_public_uint64 ]]
}

if ! need_data; then
  echo "=== Downloading datasets ==="
  bash ./scripts/download_dataset.sh
fi

if ! need_data; then
  echo "ERROR: Missing one or more dataset files under ./data/"
  exit 1
fi

echo "=== STEP: create_minimal_cmake ==="
bash ./scripts/create_minimal_cmake.sh
rm -rf build

echo "=== STEP: generate_workloads ==="
bash ./scripts/generate_workloads.sh

if [ ! -f ./generate ]; then
  echo "ERROR: ./generate missing after generate_workloads."
  exit 1
fi

echo "=== STEP: build_benchmark ==="
bash ./scripts/build_benchmark.sh

echo "=== STEP: run_milestone3_mix ==="
bash ./scripts/run_milestone3_mix.sh

echo "=== Milestone 3 benchmark finished. Plot with: python scripts/plot_milestone3_mix.py ==="
