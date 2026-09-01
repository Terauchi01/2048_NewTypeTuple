#!/usr/bin/env bash
set -euo pipefail

job_dir=${RESULT_ROOT:-results_probability_128}
pid_file="$job_dir/job.pid"
host_file="$job_dir/job.host"
launcher_log="$job_dir/launcher.log"
expected=$(wc -l < exact_layout_groups.tsv)
if [[ ! -d "$job_dir" ]]; then
  echo "not started: finished=0/$expected, files=0"
  exit 0
fi
started=$(find "$job_dir" -mindepth 2 -maxdepth 2 -type f -name result.txt 2>/dev/null | wc -l)
finished=0
while IFS= read -r -d '' result_file; do
  if grep -q '^optimal_success_probability=' "$result_file"; then
    ((finished += 1))
  fi
done < <(find "$job_dir" -mindepth 2 -maxdepth 2 -type f -name result.txt -print0 2>/dev/null)

if [[ ! -f "$pid_file" ]]; then
  echo "not started: finished=$finished/$expected, files=$started"
  exit 0
fi

job_pid=$(<"$pid_file")
job_host=$(cat "$host_file" 2>/dev/null || true)
if [[ -f "$launcher_log" ]] && grep -q "wrote $job_dir" "$launcher_log"; then
  echo "completed on ${job_host:-unknown-host}: finished=$finished/$expected"
elif [[ "$job_pid" =~ ^[0-9]+$ ]] && kill -0 "$job_pid" 2>/dev/null; then
  echo "running on ${job_host:-unknown-host}: pid=$job_pid, finished=$finished/$expected"
else
  echo "stopped or failed on ${job_host:-unknown-host}: finished=$finished/$expected"
  echo "inspect $launcher_log"
fi
