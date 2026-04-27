#!/usr/bin/env bash
# Verify shared filesystem is ready for run_milestone3_mix.sh (compute node).
# Run from repo root. Exits 0 if OK, 1 with messages if not.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

err=0
need() {
  if [[ ! -e "$1" ]]; then
    echo "ERROR: missing: $1"
    err=1
  fi
}

need_exec() {
  if [[ ! -x "$1" ]]; then
    echo "ERROR: missing or not executable: $1"
    err=1
  fi
}

need_exec "./build/benchmark"

DATASETS=(books_100M_public_uint64 fb_100M_public_uint64 osmc_100M_public_uint64)
MIX_SUFFIXES=(0.100000i_0m_mix 0.900000i_0m_mix)

for DATA in "${DATASETS[@]}"; do
  need "./data/${DATA}"
  for MIX in "${MIX_SUFFIXES[@]}"; do
    need "./data/${DATA}_ops_2M_0.000000rq_0.500000nl_${MIX}"
  done
done

if [[ "$err" -ne 0 ]]; then
  echo ""
  echo "Prepare on the login node (vis), from this repo root:"
  echo "  bash scripts/login_milestone3_prep.sh          # full: data, workloads, build"
  echo "  bash scripts/login_milestone3_prep.sh build-only # rebuild benchmark only"
  exit 1
fi

echo "Milestone 3 mix prereqs OK (data, workload ops files, build/benchmark)."
