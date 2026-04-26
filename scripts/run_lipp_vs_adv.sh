#!/usr/bin/env bash
# Compare LIPP vs HybridPGMLippAdv on one dataset (default: Facebook 100M).
# Two mixed workloads: 90% insert and 10% insert (same naming as other milestone scripts).
#
# Usage from repo root:
#   bash scripts/run_lipp_vs_adv.sh
#   DATASET=books_100M_public_uint64 bash scripts/run_lipp_vs_adv.sh
set -euo pipefail

DATASET="${DATASET:-fb_100M_public_uint64}"
BENCHMARK=build/benchmark

if [[ ! -f "$BENCHMARK" ]]; then
  echo "benchmark binary missing at $BENCHMARK; build first."
  exit 1
fi

if [[ ! -f "./data/${DATASET}" ]]; then
  echo "missing ./data/${DATASET}"
  exit 1
fi

mkdir -p ./results

MIXES=(0.900000i_0m_mix 0.100000i_0m_mix)
INDEXES=(LIPP HybridPGMLippAdv)

for MIX in "${MIXES[@]}"; do
  OPS="./data/${DATASET}_ops_2M_0.000000rq_0.500000nl_${MIX}"
  if [[ ! -f "$OPS" ]]; then
    echo "missing workload file: $OPS"
    exit 1
  fi
  for INDEX in "${INDEXES[@]}"; do
    echo "=== ${DATASET} | ${MIX} | ${INDEX} ==="
    "$BENCHMARK" "./data/${DATASET}" "$OPS" --through --csv --only "$INDEX" -r 3
  done
done

echo "Adding CSV headers where missing..."
for MIX in "${MIXES[@]}"; do
  FILE="./results/${DATASET}_ops_2M_0.000000rq_0.500000nl_${MIX}_results_table.csv"
  [[ -e "$FILE" ]] || continue
  if head -n 1 "$FILE" | grep -q "index_name"; then
    sed -i '1d' "$FILE"
  fi
  sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
  echo "Header: $FILE"
done

echo "LIPP vs HybridPGMLippAdv complete for dataset=${DATASET}"
