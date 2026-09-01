#!/usr/bin/env bash
set -euo pipefail

job_dir=${RESULT_ROOT:-results_probability_128}
pid_file="$job_dir/job.pid"
host_file="$job_dir/job.host"
launcher_log="$job_dir/launcher.log"
job_host=$(hostname)
mkdir -p "$job_dir"

if [[ -f "$launcher_log" ]] && grep -q "wrote $job_dir" "$launcher_log"; then
  echo "128 probability analysis already completed in $job_dir; use a new RESULT_ROOT to rerun"
  exit 1
fi

if [[ -f "$pid_file" ]]; then
  old_pid=$(<"$pid_file")
  old_host=$(cat "$host_file" 2>/dev/null || true)
  if [[ -n "$old_host" && "$old_host" != "$job_host" ]]; then
    echo "$job_dir belongs to host $old_host; use a different RESULT_ROOT" >&2
    exit 1
  fi
  if [[ "$old_pid" =~ ^[0-9]+$ ]] && kill -0 "$old_pid" 2>/dev/null; then
    echo "128 probability analysis is already running: pid=$old_pid"
    exit 1
  fi
fi

nohup env \
  RESULT_ROOT="$job_dir" \
  JOBS="${JOBS:-4}" \
  MAX_STATES="${MAX_STATES:-10000000}" \
  MAX_SECONDS="${MAX_SECONDS:-604800}" \
  PROGRESS_INTERVAL="${PROGRESS_INTERVAL:-500000}" \
  ./run_128_probability_matrix.sh \
  >"$launcher_log" 2>&1 &
job_pid=$!
echo "$job_pid" >"$pid_file"
echo "$job_host" >"$host_file"
echo "started 128 probability analysis on $job_host: pid=$job_pid log=$launcher_log"
