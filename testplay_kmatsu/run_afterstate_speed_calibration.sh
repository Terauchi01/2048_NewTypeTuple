#!/usr/bin/env bash
# Per-method progressive depth sweep; each individual search uses one thread.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
EVFILE="${1:-$REPO_ROOT/learn/dat/tuples6-seed0-VSE-count004.dat}"
RUN_DIR="${RUN_DIR:-$SCRIPT_DIR/results/afterstate_8way_speed_$(date +%Y%m%d_%H%M%S)}"
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
echo "RUNNING pid=$$" > "$RUN_DIR/STATUS"
echo "$$" > "$RUN_DIR/PID"
exec >> "$RUN_DIR/run.log" 2>&1
if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
  make -C "$SCRIPT_DIR" build-beam-afterstate-runner-6
fi
cat > "$RUN_DIR/parameters.txt" <<PARAMS
evaluator=$EVFILE
seed=0
methods=fixed sqrt sqrt_budget cbrt power increasing decreasing adaptive
workers=${THREADS:-8}
max_moves=${CALIBRATION_MOVES:-16}
target_ms=${TARGET_MS:-1000}
max_depth=${DEPTH_MAX:-512}
max_width=${MAX_WIDTH:-2048}
fixed_k=${FIXED_K:-64}
width_budget=${BUDGET:-100000}
PARAMS
"$SCRIPT_DIR/test_beam_search_afterstate_6" 0 1 1 fixed "${MAX_WIDTH:-2048}" "$EVFILE" \
  --threads "${THREADS:-8}" --timeout-sec 60 --k "${FIXED_K:-64}" \
  --alpha "${ALPHA:-0.4}" --w0 "${W0:-64}" --ratio "${RATIO:-2}" --sigma "${SIGMA:-1}" \
  --budget "${BUDGET:-100000}" --max-moves "${CALIBRATION_MOVES:-16}" \
  --calibrate-dir "$RUN_DIR" --max-depth "${DEPTH_MAX:-512}" --target-ms "${TARGET_MS:-1000}"
python3 - "$RUN_DIR" <<'PY'
import pathlib,csv,sys
root=pathlib.Path(sys.argv[1])
with (root/'summary.csv').open('w') as file:
    writer=csv.writer(file)
    writer.writerow(['method','selected_depth','avg_search_us','first_over_depth','status','budget'])
    for method in ('fixed','sqrt','sqrt_budget','cbrt','power','increasing','decreasing','adaptive'):
        with (root/method/'selection.csv').open() as source:
            rows=list(csv.reader(source))
        if len(rows)!=2:
            raise RuntimeError(f'Missing selection: {method}')
        writer.writerow(rows[1])
PY
