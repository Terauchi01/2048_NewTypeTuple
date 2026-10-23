#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
RUN_DIR="${RUN_DIR:-$SCRIPT_DIR/results/parallel_beam_$(date +%Y%m%d_%H%M%S)}"
EVFILE="${1:-$REPO_ROOT/learn/dat/tuples6-seed0-VSE-count004.dat}"
mkdir -p "$RUN_DIR"
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"
finish() {
 local rc=$?
 if [[ $rc -eq 0 ]]; then echo COMPLETED > "$RUN_DIR/STATUS";
 else echo "FAILED exit_code=$rc" > "$RUN_DIR/STATUS"; fi
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
echo "$$" > "$RUN_DIR/PID"
echo "STARTING pid=$$" > "$RUN_DIR/STATUS"
exec >> "$RUN_DIR/run.log" 2>&1
if [[ -n "${WAIT_FOR_RUN_DIR:-}" ]]; then
 while [[ -f "$WAIT_FOR_RUN_DIR/STATUS" ]] && [[ "$(cat "$WAIT_FOR_RUN_DIR/STATUS")" == RUNNING* ]]; do
  echo "WAITING previous_experiment=$WAIT_FOR_RUN_DIR" > "$RUN_DIR/STATUS"
  sleep 10
 done
fi
echo "RUNNING pid=$$" > "$RUN_DIR/STATUS"
if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
 make -C "$SCRIPT_DIR" build-beam-parallel-benchmark
fi
printf 'evaluator=%s\nmethods=fixed,sqrt,sqrt_budget\ndepth=12\nmax_width=2048\nfixed_k=64\nwidth_budget=100000\nthreads=1,2,4,8,16,32\ntasks_per_round=%s\nrepeats=%s\n' \
 "$EVFILE" "${BENCHMARK_TASKS:-96}" "${BENCHMARK_REPEATS:-3}" > "$RUN_DIR/parameters.txt"
"$SCRIPT_DIR/benchmark_parallel_afterstate_6" "$EVFILE" "$RUN_DIR" "${BENCHMARK_TASKS:-96}" "${BENCHMARK_REPEATS:-3}"
python3 - "$RUN_DIR" <<'PY'
import pathlib,csv,statistics,sys
root=pathlib.Path(sys.argv[1]);rows=list(csv.DictReader((root/'rounds.csv').open()))
with (root/'summary.csv').open('w') as file:
 out=csv.writer(file)
 out.writerow(['method','threads','median_searches_per_second','median_avg_search_ms','throughput_speedup','latency_ratio'])
 for method in ('fixed','sqrt','sqrt_budget'):
  def values(n,key):return [float(r[key]) for r in rows if r['method']==method and int(r['threads'])==n]
  baseline_rate=statistics.median(values(1,'searches_per_second'))
  baseline_latency=statistics.median(values(1,'avg_search_ms'))
  for n in (1,2,4,8,16,32):
   rate=statistics.median(values(n,'searches_per_second'));latency=statistics.median(values(n,'avg_search_ms'))
   out.writerow([method,n,rate,latency,rate/baseline_rate,latency/baseline_latency])
PY
