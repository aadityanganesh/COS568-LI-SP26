#!/usr/bin/env bash
# Login-node phase of the prefilter A/B.
#
# Patches the kEnablePrefilter constant in
# competitors/hybrid_pgm_lipp_advanced.h, builds the benchmark binary, and
# saves it under build/benchmark.prefilter_<on|off>. Restores the original
# header on exit. Run from repo root.
#
# After this finishes, sbatch milestone3_prefilter_ab.slurm only needs to
# RUN the two saved binaries; it does not need cmake on the compute node.
#
# Prereq on the login node: the same gcc/boost/cmake environment that
# scripts/login_milestone3_prep.sh sets up. If you have not built the
# benchmark before, run that first.
#
# Usage:
#   bash scripts/login_prefilter_ab_build.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

HEADER="competitors/hybrid_pgm_lipp_advanced.h"
BACKUP="${HEADER}.prefilter_ab.bak"

if [[ ! -f "$HEADER" ]]; then
  echo "missing $HEADER"
  exit 1
fi
if [[ ! -f scripts/build_benchmark.sh ]]; then
  echo "run from repo root (missing scripts/build_benchmark.sh)"
  exit 1
fi

echo "Backing up ${HEADER} -> ${BACKUP}"
cp -f "$HEADER" "$BACKUP"
cleanup() {
  cp -f "$BACKUP" "$HEADER"
  rm -f "$BACKUP"
}
trap cleanup EXIT

build_variant() {
  local name="$1"
  local value="$2"
  local out="build/benchmark.prefilter_${name}"

  echo ""
  echo "========== build prefilter ${name} (kEnablePrefilter = ${value}) =========="
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

  bash scripts/build_benchmark.sh

  if [[ ! -x build/benchmark ]]; then
    echo "ERROR: build/benchmark missing after build_benchmark.sh"
    exit 1
  fi

  cp -f build/benchmark "$out"
  chmod +x "$out"
  echo "saved $out"
}

build_variant "on"  "true"
build_variant "off" "false"

echo ""
echo "Both binaries ready:"
ls -l build/benchmark.prefilter_on build/benchmark.prefilter_off
echo "Now: sbatch milestone3_prefilter_ab.slurm"
