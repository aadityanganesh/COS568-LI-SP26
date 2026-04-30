#!/usr/bin/env bash
# A/B the global LIPP-membership prefilter in HybridPGMLippAdv.
#
# Patches the kEnablePrefilter constant in competitors/hybrid_pgm_lipp_advanced.h,
# rebuilds, runs scripts/run_milestone3_mix.sh, and snapshots the resulting CSVs
# into results/prefilter_ab/{on,off}/. Restores the original header on exit.
#
# No network. Run from repo root after a working build is in place.
#
# Usage:
#   bash scripts/run_prefilter_ab.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

HEADER="competitors/hybrid_pgm_lipp_advanced.h"
BACKUP="${HEADER}.prefilter_ab.bak"
OUTDIR="results/prefilter_ab"

if [[ ! -f "$HEADER" ]]; then
  echo "missing $HEADER"
  exit 1
fi

if [[ ! -f scripts/build_benchmark.sh ]]; then
  echo "run from repo root (missing scripts/build_benchmark.sh)"
  exit 1
fi
if [[ ! -f scripts/run_milestone3_mix.sh ]]; then
  echo "run from repo root (missing scripts/run_milestone3_mix.sh)"
  exit 1
fi

echo "Backing up ${HEADER} -> ${BACKUP}"
cp -f "$HEADER" "$BACKUP"
cleanup() {
  cp -f "$BACKUP" "$HEADER"
  rm -f "$BACKUP"
}
trap cleanup EXIT

mkdir -p "$OUTDIR/on" "$OUTDIR/off"

run_variant() {
  local name="$1"
  local value="$2"

  echo ""
  echo "========== prefilter ${name} (kEnablePrefilter = ${value}) =========="
  cp -f "$BACKUP" "$HEADER"

  if ! sed -i "s/kEnablePrefilter = [a-z]*;/kEnablePrefilter = ${value};/" "$HEADER"; then
    echo "sed failed"
    exit 1
  fi
  if ! grep -q "kEnablePrefilter = ${value};" "$HEADER"; then
    echo "ERROR: expected 'kEnablePrefilter = ${value};' in header after sed"
    grep "kEnablePrefilter" "$HEADER" || true
    exit 1
  fi

  echo "--- build ---"
  bash scripts/build_benchmark.sh

  rm -f results/*.csv 2>/dev/null || true
  echo "--- run_milestone3_mix (prefilter ${name}) ---"
  bash scripts/run_milestone3_mix.sh

  for src in results/*_ops_2M_*_results_table.csv; do
    [[ -f "$src" ]] || continue
    base="$(basename "$src")"
    cp -f "$src" "${OUTDIR}/${name}/${base}"
    echo "saved ${OUTDIR}/${name}/${base}"
  done
}

run_variant "on" "true"
run_variant "off" "false"

echo ""
echo "A/B done. CSVs under ${OUTDIR}/on/ and ${OUTDIR}/off/."
echo "Compare with e.g.:"
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' ${OUTDIR}/on/*_0.100000i_0m_mix_results_table.csv"
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' ${OUTDIR}/off/*_0.100000i_0m_mix_results_table.csv"
