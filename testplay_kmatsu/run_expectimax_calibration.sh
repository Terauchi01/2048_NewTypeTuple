#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
EVFILE="${1:-$REPO_ROOT/learn/dat/tuples6-seed0-VSE-count004.dat}"
RUN_DIR="${RUN_DIR:-$SCRIPT_DIR/results/expectimax_calibration_$(date +%Y%m%d_%H%M%S)}"
mkdir -p "$RUN_DIR"
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"
finish() {
  local rc=$?
  if [[ $rc -eq 0 ]]; then
    echo COMPLETED > "$RUN_DIR/STATUS"
  else
    echo "FAILED exit_code=$rc" > "$RUN_DIR/STATUS"
  fi
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
echo "RUNNING pid=$$" > "$RUN_DIR/STATUS"
echo "$$" > "$RUN_DIR/PID"
exec >> "$RUN_DIR/run.log" 2>&1
if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
  make -C "$SCRIPT_DIR" build-evaluator-audit
fi
"$SCRIPT_DIR/audit_evaluator_6" calibrate "$EVFILE" "$RUN_DIR"
