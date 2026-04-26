#!/bin/bash
# Milestone 2 — RUN ON COMPUTE NODE (sbatch)
#
# Usage:
#   sbatch milestone2.slurm
#
# This job runs only Facebook mixed workloads for:
#   DynamicPGM, LIPP, HybridPGMLippAdv (see scripts/run_milestone2_fb.sh)

#SBATCH --job-name=milestone2
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=32G
#SBATCH --time=08:00:00
#SBATCH --output=slurm-%j.out

set -eo pipefail

cd "${SLURM_SUBMIT_DIR}"

# Optional: pyenv shims can interfere with module/conda toolchain in batch.
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
# Prefer system cmake on Adroit to avoid CMake 4.x FindBoost incompatibilities.
if [ -x /usr/bin/cmake ]; then
  export PATH="/usr/bin:${MODULE_PATH}"
else
  # Fallback to conda env if system cmake is unavailable.
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
    echo "--- conda env list ---"
    conda env list 2>&1 || true
    exit 1
  fi
  export PATH="$(dirname "${COS568_CMAKE}"):${MODULE_PATH}"
fi

echo "=== cmake / gcc check ==="
command -v cmake
command -v gcc
cmake --version | head -1
gcc --version | head -1

chmod +x scripts/*.sh

if [ ! -f data/fb_100M_public_uint64 ]; then
  echo "=== data/fb_100M_public_uint64 missing; downloading datasets ==="
  bash ./scripts/download_dataset.sh
fi

echo "=== STEP: create_minimal_cmake ==="
bash ./scripts/create_minimal_cmake.sh
rm -rf build

echo "=== STEP: generate_workloads ==="
bash ./scripts/generate_workloads.sh

if [ ! -f ./generate ]; then
  echo "ERROR: workload generator binary ./generate is missing after generate_workloads."
  exit 1
fi

echo "=== STEP: build_benchmark ==="
bash ./scripts/build_benchmark.sh

echo "=== STEP: run_milestone2_fb ==="
bash ./scripts/run_milestone2_fb.sh

echo "=== Milestone 2 run finished. Check ./results/ and slurm output ==="
