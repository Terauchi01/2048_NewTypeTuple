#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 CONDITION_NAME RANK_CELLS" >&2
  exit 1
fi

condition_name=$1
rank_cells=$2
result_root=${RESULT_ROOT:-results_probability_128}
condition_dir="$result_root/$condition_name"
mkdir -p "$condition_dir"

/usr/bin/time \
  -o "$condition_dir/resources.txt" \
  -f 'wall_seconds=%e\npeak_rss_kb=%M' \
  ./exact_second_32768 \
    --rank-cells "$rank_cells" \
    --protected-max-exponent 14 \
    --target-exponent 7 \
    --mode probability \
    --spawn-four-probability 0.1 \
    --max-states "${MAX_STATES:-10000000}" \
    --max-seconds "${MAX_SECONDS:-604800}" \
    --progress-interval "${PROGRESS_INTERVAL:-500000}" \
    >"$condition_dir/result.txt" \
    2>"$condition_dir/progress.txt"
