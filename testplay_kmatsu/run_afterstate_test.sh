#!/usr/bin/env bash
set -euo pipefail

if [[ $# -gt 1 ]]; then
  echo "Usage: $0 [seed0-double-6tuple-evaluator.dat[.xz]]" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
if [[ $# -eq 1 ]]; then
  EVFILE="$1"
elif [[ -f "$REPO_ROOT/tuples6-seed0-VSE-count004.dat" ]]; then
  EVFILE="$REPO_ROOT/tuples6-seed0-VSE-count004.dat"
else
  EVFILE="$REPO_ROOT/tuples6-seed0-VSE-count004.dat.xz"
fi
if [[ "$EVFILE" != /* ]]; then
  EVFILE="$(cd "$(dirname "$EVFILE")" && pwd -P)/$(basename "$EVFILE")"
fi
if [[ ! -r "$EVFILE" ]]; then
  echo "Evaluator file is not readable: $EVFILE" >&2
  exit 2
fi

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
RUN_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/afterstate_test_$TIMESTAMP}"
mkdir -p "$RUN_DIR/calibration"
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"
STATUS_FILE="$RUN_DIR/STATUS"
CALIBRATION_CSV="$RUN_DIR/calibration.csv"
GAME_CSV="$RUN_DIR/games_seed0_count10.csv"
RUN_LOG="$RUN_DIR/run.log"
echo "RUNNING pid=$$ started=$(date '+%Y-%m-%dT%H:%M:%S%z')" > "$STATUS_FILE"
echo "$$" > "$RUN_DIR/PID"
echo "method,depth,runner_status,exit_code,moves,search_time_us,avg_search_us,log" > "$CALIBRATION_CSV"

finish() {
  local rc=$?
  trap - EXIT
  if [[ $rc -eq 0 ]]; then
    echo "COMPLETED finished=$(date '+%Y-%m-%dT%H:%M:%S%z')" > "$STATUS_FILE"
  else
    echo "FAILED exit_code=$rc finished=$(date '+%Y-%m-%dT%H:%M:%S%z')" > "$STATUS_FILE"
  fi
  exit "$rc"
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
exec >> "$RUN_LOG" 2>&1

BEAM_BIN="${BEAM_BIN:-$SCRIPT_DIR/test_beam_search_afterstate_6}"
MAX_WIDTH="${MAX_WIDTH:-2048}"
FIXED_K="${FIXED_K:-64}"
TARGET_MS="${TARGET_MS:-1000}"
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"
CALIBRATION_MOVES="${CALIBRATION_MOVES:-16}"
DEPTH_MAX="${DEPTH_MAX:-20}"
if [[ ! -x "$BEAM_BIN" ]]; then
  echo "Afterstate runner is not executable: $BEAM_BIN" >&2
  exit 2
fi

kv_value() {
  local file=$1 key=$2
  awk -F, -v key="$key" '/^game,/{for(i=1;i<NF;i+=2) if($i==key){print $(i+1); exit}}' "$file"
}

echo "run_dir=$RUN_DIR evaluator=$EVFILE method=fixed k=$FIXED_K target_ms=$TARGET_MS"
selected_depth=0
selected_distance=""
for ((depth=1; depth<=DEPTH_MAX; ++depth)); do
  log="$RUN_DIR/calibration/depth${depth}.log"
  set +e
  "$BEAM_BIN" 0 1 "$depth" fixed "$MAX_WIDTH" "$EVFILE" \
    --threads 1 --timeout-sec "$TIMEOUT_SEC" --k "$FIXED_K" \
    --max-moves "$CALIBRATION_MOVES" 2>&1 | tee "$log"
  rc=${PIPESTATUS[0]}
  set -e
  status="$(kv_value "$log" status || true)"
  moves="$(kv_value "$log" moves || true)"
  total="$(kv_value "$log" search_time_us || true)"
  avg=""
  if [[ "${moves:-0}" -gt 0 && "$total" =~ ^[0-9]+$ ]]; then
    avg="$(awk -v total="$total" -v moves="$moves" 'BEGIN{printf "%.3f",total/moves}')"
    distance="$(awk -v avg="$avg" -v target="$TARGET_MS" 'BEGIN{d=avg-target*1000;if(d<0)d=-d;printf "%.3f",d}')"
    if [[ -z "$selected_distance" ]] || \
       [[ "$(awk -v a="$distance" -v b="$selected_distance" 'BEGIN{print (a < b)}')" == 1 ]]; then
      selected_depth="$depth"
      selected_distance="$distance"
    fi
  fi
  echo "fixed,$depth,${status:-error},$rc,${moves:-0},${total:-0},$avg,calibration/depth${depth}.log" >> "$CALIBRATION_CSV"
  echo "calibration depth=$depth avg_search_us=${avg:-n/a} status=${status:-error} rc=$rc"
  if [[ "$rc" -eq 124 ]]; then
    echo "depth=$depth exceeded per-move timeout; stop calibration"
    break
  fi
  if [[ "$rc" -ne 0 ]]; then
    echo "depth=$depth failed; stop calibration"
    exit "$rc"
  fi
  if [[ -n "$avg" ]] && [[ "$(awk -v avg="$avg" -v target="$TARGET_MS" 'BEGIN{print (avg >= target*1000)}')" == 1 ]]; then
    break
  fi
done

if [[ "$selected_depth" -lt 1 ]]; then
  echo "No usable calibration depth found" >&2
  exit 1
fi
echo "selected_depth=$selected_depth nearest_target_us=$selected_distance"
echo "Running 10 complete games with seeds 0..9 at depth=$selected_depth"
set +e
"$BEAM_BIN" 0 10 "$selected_depth" fixed "$MAX_WIDTH" "$EVFILE" \
  --threads 1 --timeout-sec "$TIMEOUT_SEC" --k "$FIXED_K" \
  --max-moves 0 2>&1 | tee "$GAME_CSV"
rc=${PIPESTATUS[0]}
set -e
if [[ "$rc" -ne 0 ]]; then
  echo "10-game run exited with status $rc" >&2
  exit "$rc"
fi
completed="$(awk -F, '/^game,/ {for(i=1;i<NF;i+=2) if($i=="status" && $(i+1)=="terminal") n++} END{print n+0}' "$GAME_CSV")"
if [[ "$completed" -ne 10 ]]; then
  echo "Expected 10 terminal games, found $completed" >&2
  exit 1
fi
awk -F, '/^game,/ {for(i=1;i<NF;i+=2) v[$i]=$(i+1);n++;s+=v["sco"];b+=v["big"];m+=v["moves"];t+=v["search_time_us"];delete v} END{if(n)printf "games=%d avg_score=%.2f avg_big_tile=%.2f avg_moves=%.2f total_search_time_us=%.0f\n",n,s/n,b/n,m/n,t}' "$GAME_CSV" | tee "$RUN_DIR/summary.txt"
echo "results=$RUN_DIR"
