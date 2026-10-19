#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
RUN_DIR="${RUN_DIR:-$SCRIPT_DIR/results/beam_comparison_$TIMESTAMP}"
mkdir -p "$RUN_DIR/calibration" "$RUN_DIR/games"
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"
ARGS=("$@")
if [[ ${#ARGS[@]} -eq 1 && "${ARGS[0]}" != /* ]]; then
  ARGS[0]="$(cd "$(dirname "${ARGS[0]}")" && pwd -P)/$(basename "${ARGS[0]}")"
fi

echo "STARTING launched=$(date '+%Y-%m-%dT%H:%M:%S%z')" > "$RUN_DIR/STATUS"
if command -v screen >/dev/null 2>&1; then
  session="beam_$(date +%Y%m%d%H%M%S)_$$"
  screen -DmS "$session" /usr/bin/env RUN_DIR="$RUN_DIR" \
    "$SCRIPT_DIR/run_beam_comparison.sh" "${ARGS[@]}"
  echo "$session" > "$RUN_DIR/SCREEN_SESSION"
  echo "SCREEN_SESSION=$session"
else
  nohup env RUN_DIR="$RUN_DIR" "$SCRIPT_DIR/run_beam_comparison.sh" "${ARGS[@]}" \
    > "$RUN_DIR/launcher.log" 2>&1 < /dev/null &
  pid=$!
  echo "$pid" > "$RUN_DIR/PID"
  echo "PID=$pid"
fi
echo "STATUS=$RUN_DIR/STATUS"
echo "RESULT_DIR=$RUN_DIR"
