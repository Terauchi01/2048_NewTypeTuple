#!/usr/bin/env bash
set -euo pipefail

remote_host=${REMOTE_HOST:-172.21.52.93}
remote_dir=${REMOTE_DIR:-terauchi/2048_NewTypeTuple}
if [[ ! "$remote_host" =~ ^[A-Za-z0-9_.@-]+$ ]]; then
  echo "REMOTE_HOST contains unsupported characters" >&2
  exit 1
fi
if [[ ! "$remote_dir" =~ ^[A-Za-z0-9_./-]+$ || "$remote_dir" == *..* ]]; then
  echo "REMOTE_DIR must be a simple path without '..' or shell metacharacters" >&2
  exit 1
fi
files=(
  exact_second_32768.cpp
  Makefile.exact_analysis
  EXACT_ANALYSIS_SPEC.md
  EXACT_128_PROBABILITY_RESULTS.md
  exact_layout_groups.tsv
  run_128_probability_condition.sh
  run_128_probability_matrix.sh
  start_128_probability_background.sh
  check_128_probability_job.sh
  summarize_128_probabilities.sh
  RUN_128_PROBABILITY_REMOTE.md
)

for file in "${files[@]}"; do
  [[ -f "$file" ]] || { echo "missing transfer file: $file" >&2; exit 1; }
done

ssh "$remote_host" "mkdir -p '$remote_dir'"
scp -- "${files[@]}" "$remote_host:$remote_dir/"
ssh "$remote_host" \
  "cd '$remote_dir' && chmod +x run_128_probability_condition.sh run_128_probability_matrix.sh start_128_probability_background.sh check_128_probability_job.sh summarize_128_probabilities.sh"
echo "transferred ${#files[@]} files to $remote_host:$remote_dir"
