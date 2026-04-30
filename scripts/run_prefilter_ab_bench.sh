#!/usr/bin/env bash
# Compute-node phase of the prefilter A/B. No cmake, no compile.
#
# Expects build/benchmark.prefilter_on and build/benchmark.prefilter_off to
# already exist (built on the login node via scripts/login_prefilter_ab_build.sh).
# Runs scripts/run_milestone3_mix.sh with each binary and snapshots the CSVs
# under results/prefilter_ab/{on,off}/.
#
# Usage from repo root:
#   bash scripts/run_prefilter_ab_bench.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

OUTDIR="results/prefilter_ab"
ON_BIN="build/benchmark.prefilter_on"
OFF_BIN="build/benchmark.prefilter_off"

if [[ ! -x "$ON_BIN" ]] || [[ ! -x "$OFF_BIN" ]]; then
  echo "ERROR: missing pre-built binaries:"
  ls -l "$ON_BIN" "$OFF_BIN" 2>&1 || true
  echo ""
  echo "Run on the login node first:"
  echo "  bash scripts/login_prefilter_ab_build.sh"
  exit 1
fi

if [[ ! -f scripts/run_milestone3_mix.sh ]]; then
  echo "run from repo root (missing scripts/run_milestone3_mix.sh)"
  exit 1
fi

mkdir -p "$OUTDIR/on" "$OUTDIR/off"

run_variant() {
  local name="$1"
  local bin="$2"

  echo ""
  echo "========== run prefilter ${name} (${bin}) =========="

  rm -f results/*.csv 2>/dev/null || true
  BENCHMARK="$bin" bash scripts/run_milestone3_mix.sh

  for src in results/*_ops_2M_*_results_table.csv; do
    [[ -f "$src" ]] || continue
    base="$(basename "$src")"
    cp -f "$src" "${OUTDIR}/${name}/${base}"
    echo "saved ${OUTDIR}/${name}/${base}"
  done
}

run_variant "on"  "$ON_BIN"
run_variant "off" "$OFF_BIN"

echo ""
echo "A/B done. CSVs under ${OUTDIR}/on/ and ${OUTDIR}/off/."
echo "Compare with e.g.:"
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' ${OUTDIR}/on/*_0.100000i_0m_mix_results_table.csv"
echo "  grep -E '^(LIPP|HybridPGMLippAdv),' ${OUTDIR}/off/*_0.100000i_0m_mix_results_table.csv"
