#!/usr/bin/env bash
# Milestone 3: mixed workloads (10% and 90% insert) on books, FB, OSMC.
# Compares DynamicPGM, LIPP, naive HybridPGMLipp, and HybridPGMLippAdv (sorted flush).
# Run from repo root after build. Uses -r 3 per README.
set -euo pipefail

BENCHMARK=build/benchmark
if [[ ! -f "$BENCHMARK" ]]; then
  echo "benchmark binary missing; run scripts/build_benchmark.sh first"
  exit 1
fi

mkdir -p ./results

DATASETS=(books_100M_public_uint64 fb_100M_public_uint64 osmc_100M_public_uint64)
INDEXES=(DynamicPGM LIPP HybridPGMLipp HybridPGMLippAdv)
MIX_SUFFIXES=(0.100000i_0m_mix 0.900000i_0m_mix)

echo "Removing old Milestone 3 mix CSVs (same filenames as new runs)..."
for DATA in "${DATASETS[@]}"; do
  for MIX in "${MIX_SUFFIXES[@]}"; do
    rm -f "./results/${DATA}_ops_2M_0.000000rq_0.500000nl_${MIX}_results_table.csv"
  done
done

for DATA in "${DATASETS[@]}"; do
  for MIX in "${MIX_SUFFIXES[@]}"; do
    for INDEX in "${INDEXES[@]}"; do
      echo "=== ${DATA} | ${MIX} | ${INDEX} ==="
      "$BENCHMARK" "./data/${DATA}" "./data/${DATA}_ops_2M_0.000000rq_0.500000nl_${MIX}" \
        --through --csv --only "$INDEX" -r 3
    done
  done
done

echo "Adding CSV headers where missing (Milestone 3 mix files only)..."
for DATA in "${DATASETS[@]}"; do
  for MIX in "${MIX_SUFFIXES[@]}"; do
    FILE="./results/${DATA}_ops_2M_0.000000rq_0.500000nl_${MIX}_results_table.csv"
    [[ -e "$FILE" ]] || continue
    if head -n 1 "$FILE" | grep -q "index_name"; then
      sed -i '1d' "$FILE"
    fi
    sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
    echo "Header: $FILE"
  done
done

echo "Milestone 3 mix benchmark complete."
