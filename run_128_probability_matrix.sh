#!/usr/bin/env bash
set -euo pipefail

result_root=${RESULT_ROOT:-results_probability_128}
jobs=${JOBS:-4}
export RESULT_ROOT="$result_root"
export MAX_STATES=${MAX_STATES:-10000000}
export MAX_SECONDS=${MAX_SECONDS:-604800}
export PROGRESS_INTERVAL=${PROGRESS_INTERVAL:-500000}

make -f Makefile.exact_analysis exact_second_32768
mkdir -p "$result_root"

awk -F '\t' '{print $2, $3}' exact_layout_groups.tsv |
  xargs -P "$jobs" -n 2 ./run_128_probability_condition.sh

echo "wrote $result_root"
