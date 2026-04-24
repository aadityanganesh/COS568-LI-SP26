#!/bin/bash
# Milestone 1 — RUN ON COMPUTE NODE (sbatch)
#
# Adroit compute nodes: no wget; often no system cmake. Use conda env "cos568" with cmake.
#
# ONE-TIME on LOGIN:
#   module load anaconda3/2023.3
#   conda create -y -n cos568 -c conda-forge cmake
#   conda activate cos568 && which cmake && cmake --version
#
# ONE-TIME: bash scripts/download_dataset.sh  (from repo root, on login)
#
# Then: sbatch milestone1.slurm

#SBATCH --job-name=milestone1
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=32G
#SBATCH --time=08:00:00
#SBATCH --output=slurm-%j.out

set -eo pipefail

cd "${SLURM_SUBMIT_DIR}"

# Optional: pyenv shims in PATH (seen on your account) can break conda/gcc in batch jobs.
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

# Non-interactive Slurm: hook is more reliable than conda activate alone
if [ -x "${CONDA_EXE}" ]; then
  eval "$("${CONDA_EXE}" shell.bash hook 2>/dev/null)" || true
fi

echo "=== STEP: activate cos568 (or use env path) ==="
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
      echo "Using cmake without activate: ${COS568_CMAKE}"
      break
    fi
  done
fi

if [ -z "${COS568_CMAKE}" ] || [ ! -x "${COS568_CMAKE}" ]; then
  echo "ERROR: No cmake in conda env cos568."
  echo "--- conda env list ---"
  conda env list 2>&1 || true
  echo "--- look for envs under home ---"
  ls -la "${HOME}/.conda/envs/" 2>&1 || true
  echo "On LOGIN: module load anaconda3/2023.3 && conda create -y -n cos568 -c conda-forge cmake"
  exit 1
fi

# cmake from cos568 first; then gcc/boost from modules (must stay before any stray shims)
export PATH="$(dirname "${COS568_CMAKE}"):${MODULE_PATH}"

echo "=== cmake / gcc check ==="
command -v cmake
command -v gcc
cmake --version | head -1
gcc --version | head -1

chmod +x scripts/*.sh

if [ ! -f data/fb_100M_public_uint64 ] || \
   [ ! -f data/books_100M_public_uint64 ] || \
   [ ! -f data/osmc_100M_public_uint64 ]; then
  echo "ERROR: Missing dataset files under ./data/"
  exit 1
fi

echo "=== STEP: create_minimal_cmake ==="
bash ./scripts/create_minimal_cmake.sh
rm -rf build

echo "=== STEP: generate_workloads ==="
bash ./scripts/generate_workloads.sh

echo "=== STEP: build_benchmark ==="
bash ./scripts/build_benchmark.sh

echo "=== STEP: run_benchmarks ==="
bash ./scripts/run_benchmarks.sh

echo "=== Benchmark finished. Check ./results/ ==="
