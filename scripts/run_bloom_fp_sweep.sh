#!/usr/bin/env bash
# Automated Bloom fp sweep: patch k_bloom_fp in hybrid_pgm_lipp_advanced.h, rebuild,
# run LIPP vs Adv (same as run_lipp_vs_adv.sh), save CSVs under results/bloom_fp_sweep/.
#
# No network. Run from repo root.
#
# Usage:
#   bash scripts/run_bloom_fp_sweep.sh
#   BLOOM_FP_SWEEP="0.02 0.01 0.005" DATASET=fb_100M_public_uint64 bash scripts/run_bloom_fp_sweep.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

HEADER="competitors/hybrid_pgm_lipp_advanced.h"
BACKUP="${HEADER}.sweepbak"
OUTDIR="results/bloom_fp_sweep"
FPS="${BLOOM_FP_SWEEP:-0.02 0.01 0.005}"
DATASET="${DATASET:-fb_100M_public_uint64}"
export DATASET

if [[ ! -f "$HEADER" ]]; then
  echo "missing $HEADER"
  exit 1
fi

if [[ ! -f "build/benchmark" ]] && [[ ! -f scripts/build_benchmark.sh ]]; then
  echo "run from repo root"
  exit 1
fi

echo "Backing up ${HEADER} -> ${BACKUP}"
cp -f "$HEADER" "$BACKUP"
cleanup() {
  cp -f "$BACKUP" "$HEADER"
  rm -f "$BACKUP"
}
trap cleanup EXIT

mkdir -p "$OUTDIR"

for fp in $FPS; do
  echo ""
  echo "========== Bloom fp = ${fp} =========="
  cp -f "$BACKUP" "$HEADER"
  if ! sed -i "s/k_bloom_fp = [0-9.eE+-]*;/k_bloom_fp = ${fp};/" "$HEADER"; then
    echo "sed failed"
    exit 1
  fi
  if ! grep -q "k_bloom_fp = ${fp};" "$HEADER"; then
    echo "ERROR: expected k_bloom_fp = ${fp}; in header after sed"
    exit 1
  fi

  echo "--- build ---"
  bash scripts/build_benchmark.sh

  rm -f results/*.csv 2>/dev/null || true
  echo "--- benchmark (LIPP vs Adv, dataset=${DATASET}) ---"
  bash scripts/run_lipp_vs_adv.sh

  for mix in 0.900000i_0m_mix 0.100000i_0m_mix; do
    src="results/${DATASET}_ops_2M_0.000000rq_0.500000nl_${mix}_results_table.csv"
    if [[ -f "$src" ]]; then
      base="$(basename "$src" .csv)"
      dest="${OUTDIR}/${base}_fp_${fp}.csv"
      cp -f "$src" "$dest"
      echo "saved $dest"
    else
      echo "WARN: missing $src"
    fi
  done
done

echo ""
echo "Sweep done. CSVs under ${OUTDIR}/"
echo "Restore default fp in ${HEADER} (from backup) on EXIT."
