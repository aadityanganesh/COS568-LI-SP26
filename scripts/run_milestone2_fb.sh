#!/usr/bin/env bash
set -euo pipefail

echo "Running Milestone 2 benchmark set (Facebook mixed workloads; includes HybridPGMLippAdv)..."

BENCHMARK=build/benchmark
if [[ ! -f "$BENCHMARK" ]]; then
  echo "benchmark binary does not exist at $BENCHMARK"
  exit 1
fi

DATA=fb_100M_public_uint64
mkdir -p ./results

run_one() {
  local index="$1"
  echo "Index: $index | workload: 0.9 insert mixed"
  "$BENCHMARK" "./data/$DATA" "./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix" --through --csv --only "$index" -r 3

  echo "Index: $index | workload: 0.1 insert mixed"
  "$BENCHMARK" "./data/$DATA" "./data/${DATA}_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix" --through --csv --only "$index" -r 3
}

for INDEX in DynamicPGM LIPP HybridPGMLipp HybridPGMLippAdv; do
  run_one "$INDEX"
done

for FILE in ./results/"${DATA}"*mix*_results_table.csv; do
  [[ -e "$FILE" ]] || continue
  if head -n 1 "$FILE" | grep -q "index_name"; then
    sed -i '1d' "$FILE"
  fi
  sed -i '1s/^/index_name,build_time_ns1,build_time_ns2,build_time_ns3,index_size_bytes,mixed_throughput_mops1,mixed_throughput_mops2,mixed_throughput_mops3,search_method,value\n/' "$FILE"
  echo "Header set for $FILE"
done

echo "Milestone 2 benchmark run complete."
