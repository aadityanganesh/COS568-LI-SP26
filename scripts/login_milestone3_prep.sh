#!/usr/bin/env bash
# Run on the cluster LOGIN NODE (vis) before sbatch milestone3*.slurm.
#
# Sets up the same compiler/cmake environment as the old all-in-one Slurm job,
# then builds the benchmark (and optionally workloads) on shared $HOME / project FS
# so compute jobs only run evaluation.
#
# Usage from repo root:
#   bash scripts/login_milestone3_prep.sh              # full: data, minimal cmake, workloads, build
#   bash scripts/login_milestone3_prep.sh build-only # cmake+make only (data + *_ops_2M_* mix files must exist)
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

MODE="${1:-full}"

export PATH=$(echo "${PATH:-}" | tr ':' '\n' | grep -v '/\.pyenv/' | paste -sd: -)
export PATH="/usr/bin:/bin:/usr/local/bin:${PATH:-}"

module purge

echo "=== load gcc + boost ==="
module load gcc/11 boost/1.85.0
MODULE_PATH="$PATH"

ANACONDA_ROOT=""
CONDA_EXE=""

load_anaconda_for_cmake() {
  echo "=== load anaconda (cmake fallback) ==="
  if ! module load anaconda3/2023.3; then
    echo "ERROR: module load anaconda3/2023.3 failed."
    exit 1
  fi

  ANACONDA_ROOT="${CONDA_PREFIX:-/usr/licensed/anaconda3/2023.3}"
  CONDA_EXE="${ANACONDA_ROOT}/bin/conda"

  if [[ ! -f "${ANACONDA_ROOT}/etc/profile.d/conda.sh" ]]; then
    echo "ERROR: missing ${ANACONDA_ROOT}/etc/profile.d/conda.sh"
    exit 1
  fi

  # shellcheck source=/dev/null
  source "${ANACONDA_ROOT}/etc/profile.d/conda.sh"

  if [[ -x "${CONDA_EXE}" ]]; then
    eval "$("${CONDA_EXE}" shell.bash hook 2>/dev/null)" || true
  fi
}

echo "=== select cmake ==="
if [[ -x /usr/bin/cmake ]]; then
  export PATH="/usr/bin:${MODULE_PATH}"
else
  load_anaconda_for_cmake
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
        export CONDA_PREFIX="${ENV_ROOT}"
        break
      fi
    done
  fi
  if [[ -z "${COS568_CMAKE}" ]] || [[ ! -x "${COS568_CMAKE}" ]]; then
    echo "ERROR: No usable cmake found."
    conda env list 2>&1 || true
    exit 1
  fi
  export PATH="$(dirname "${COS568_CMAKE}"):${MODULE_PATH}"
fi

command -v cmake
command -v gcc

chmod +x scripts/*.sh 2>/dev/null || true

if [[ "$MODE" == "build-only" ]]; then
  echo "=== build-only: skipping download / generate_workloads ==="
elif [[ "$MODE" == "full" ]]; then
  need_data() {
    [[ -f data/books_100M_public_uint64 ]] && [[ -f data/fb_100M_public_uint64 ]] && [[ -f data/osmc_100M_public_uint64 ]]
  }

  if ! need_data; then
    echo "=== downloading datasets ==="
    bash ./scripts/download_dataset.sh
  fi

  if ! need_data; then
    echo "ERROR: Missing one or more dataset files under ./data/"
    exit 1
  fi

  echo "=== create_minimal_cmake ==="
  bash ./scripts/create_minimal_cmake.sh

  echo "=== clean build dir (generate + benchmark share build/) ==="
  rm -rf build

  echo "=== generate_workloads ==="
  bash ./scripts/generate_workloads.sh

  if [[ ! -f ./build/generate ]]; then
    echo "ERROR: ./build/generate missing after generate_workloads."
    exit 1
  fi
else
  echo "ERROR: unknown mode '$MODE'. Use 'full' (default) or 'build-only'."
  exit 1
fi

echo "=== build_benchmark ==="
bash ./scripts/build_benchmark.sh

if [[ ! -x ./build/benchmark ]]; then
  echo "ERROR: ./build/benchmark missing after build."
  exit 1
fi

echo "=== login prep done. On compute: sbatch milestone3.slurm  OR  sbatch milestone3_bench_only.slurm ==="
