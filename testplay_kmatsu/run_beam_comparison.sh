#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"

if [[ $# -gt 1 ]]; then
  echo "Usage: $0 [seed0-double-6tuple-evaluator.dat[.xz]]" >&2
  exit 2
fi

if [[ $# -eq 1 ]]; then
  EVFILE="$1"
elif [[ -z "${EVFILE:-}" ]]; then
  if [[ -f "$REPO_ROOT/tuples6-seed0-VSE-count004.dat" ]]; then
    EVFILE="$REPO_ROOT/tuples6-seed0-VSE-count004.dat"
  else
    EVFILE="$REPO_ROOT/tuples6-seed0-VSE-count004.dat.xz"
  fi
fi
if [[ "$EVFILE" != /* ]]; then
  EVFILE="$(cd "$(dirname "$EVFILE")" && pwd -P)/$(basename "$EVFILE")"
fi

RUN_DIR="${RUN_DIR:-$SCRIPT_DIR/results/beam_comparison_$TIMESTAMP}"
mkdir -p "$RUN_DIR/calibration" "$RUN_DIR/games"
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"
RUN_LOG="$RUN_DIR/run.log"
STATUS_FILE="$RUN_DIR/STATUS"
echo "$$" > "$RUN_DIR/PID"
echo "RUNNING pid=$$ started=$(date '+%Y-%m-%dT%H:%M:%S%z')" > "$STATUS_FILE"

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

BEAM_BIN="${BEAM_BIN:-$SCRIPT_DIR/test_beam_search_6}"
if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
  make -C "$SCRIPT_DIR" beam
fi
if [[ ! -x "$BEAM_BIN" ]]; then
  echo "Beam runner is not executable: $BEAM_BIN" >&2
  exit 2
fi
if [[ ! -r "$EVFILE" ]]; then
  echo "Evaluator file is not readable: $EVFILE" >&2
  exit 2
fi

MAX_WIDTH="${MAX_WIDTH:-2048}"
FIXED_K="${FIXED_K:-64}"
ALPHA="${ALPHA:-0.4}"
W0="${W0:-64}"
RATIO="${RATIO:-2}"
SIGMA="${SIGMA:-1}"
DEPTH_MIN="${DEPTH_MIN:-1}"
DEPTH_MAX="${DEPTH_MAX:-10}"
CALIBRATION_MOVES="${CALIBRATION_MOVES:-16}"
TARGET_MS="${TARGET_MS:-1000}"
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"
METHODS=(fixed sqrt cbrt power increasing decreasing adaptive)

CALIBRATION_CSV="$RUN_DIR/calibration.csv"
SUMMARY_CSV="$RUN_DIR/summary.csv"
if [[ ! -s "$CALIBRATION_CSV" ]]; then
  echo "method,depth,runner_status,exit_code,moves,search_time_us,avg_search_us,distance_us,log" > "$CALIBRATION_CSV"
fi
if [[ ! -s "$SUMMARY_CSV" ]]; then
  echo "method,selected_depth,calibration_avg_us,calibration_status,status,completed_games,avg_score,avg_big_tile,avg_moves,total_search_time_us" > "$SUMMARY_CSV"
fi

kv_value() {
  local file=$1 key=$2
  awk -F, -v key="$key" '/^game,/{for(i=1;i<NF;i+=2) if($i==key){print $(i+1); exit}}' "$file"
}

COMMON_ARGS=(--threads 1 --timeout-sec "$TIMEOUT_SEC" --k "$FIXED_K"
  --alpha "$ALPHA" --w0 "$W0" --ratio "$RATIO" --sigma "$SIGMA")

overall_failure=0
echo "run_dir=$RUN_DIR"
echo "evaluator=$EVFILE target_ms=$TARGET_MS max_width=$MAX_WIDTH"

for method in "${METHODS[@]}"; do
  if awk -F, -v method="$method" '$1==method && $5=="completed"{found=1} END{exit !found}' "$SUMMARY_CSV"; then
    echo "[$method] already completed; resume skips it"
    continue
  fi

  echo "[$method] calibration"
  for ((depth=DEPTH_MIN; depth<=DEPTH_MAX; ++depth)); do
    previous="$(awk -F, -v method="$method" -v depth="$depth" '$1==method && $2==depth{line=$0} END{print line}' "$CALIBRATION_CSV")"
    if [[ -n "$previous" ]]; then
      previous_rc="$(awk -F, '{print $4}' <<< "$previous")"
      echo "[$method] depth=$depth already calibrated rc=$previous_rc"
      if [[ "$previous_rc" -ne 0 ]]; then break; fi
      continue
    fi

    calibration_log="$RUN_DIR/calibration/${method}_depth${depth}.log"
    set +e
    "$BEAM_BIN" 0 1 "$depth" "$method" "$MAX_WIDTH" "$EVFILE" \
      "${COMMON_ARGS[@]}" --max-moves "$CALIBRATION_MOVES" 2>&1 | tee "$calibration_log"
    runner_rc=${PIPESTATUS[0]}
    set -e
    runner_status="$(kv_value "$calibration_log" status || true)"
    moves="$(kv_value "$calibration_log" moves || true)"
    search_time_us="$(kv_value "$calibration_log" search_time_us || true)"
    avg_search_us=""
    distance_us=""
    if [[ "$runner_rc" -eq 0 && "${moves:-0}" -gt 0 && "$search_time_us" =~ ^[0-9]+$ ]]; then
      timing="$(awk -v total="$search_time_us" -v moves="$moves" -v target="$TARGET_MS" \
        'BEGIN{avg=total/moves; target_us=target*1000; diff=avg-target_us; if(diff<0)diff=-diff; printf "%.3f %.3f\n",avg,diff}')"
      read -r avg_search_us distance_us <<< "$timing"
    fi
    echo "$method,$depth,${runner_status:-error},$runner_rc,${moves:-0},${search_time_us:-0},${avg_search_us:-},${distance_us:-},calibration/${method}_depth${depth}.log" >> "$CALIBRATION_CSV"
    if [[ "$runner_rc" -eq 124 ]]; then
      echo "[$method] depth=$depth reached 60-second move deadline; stopping sweep"
      break
    fi
    if [[ "$runner_rc" -ne 0 ]]; then
      echo "[$method] depth=$depth failed rc=$runner_rc; stopping sweep"
      break
    fi
  done

  best="$(awk -F, -v method="$method" '
    $1==method && $4==0 && $5>0 && $8!="" {
      if (!found || $8+0 < best_distance) {
        found=1; best_distance=$8+0; best_depth=$2; best_avg=$7; best_status=$3
      }
    }
    END {if(found) printf "%s %s %s\n",best_depth,best_avg,best_status}
  ' "$CALIBRATION_CSV")"
  if [[ -z "$best" ]]; then
    echo "[$method] no valid calibration depth"
    echo "$method,,,no_candidate,failed,0,,,,0" >> "$SUMMARY_CSV"
    overall_failure=1
    continue
  fi
  read -r selected_depth calibration_avg calibration_status <<< "$best"
  echo "[$method] selected depth=$selected_depth avg_search_us=$calibration_avg"

  method_failed=0
  game_files=()
  for seed in {0..9}; do
    game_file="$RUN_DIR/games/${method}_seed${seed}.csv"
    game_files+=("$game_file")
    if [[ -s "$game_file" ]] && grep -q '^game,' "$game_file" && \
       grep -q ',status,terminal,' "$game_file"; then
      echo "[$method] seed=$seed already complete; resume skips it"
      continue
    fi
    set +e
    "$BEAM_BIN" "$seed" 1 "$selected_depth" "$method" "$MAX_WIDTH" "$EVFILE" \
      "${COMMON_ARGS[@]}" --max-moves 0 2>&1 | tee "$game_file"
    runner_rc=${PIPESTATUS[0]}
    set -e
    if [[ "$runner_rc" -ne 0 ]] || ! grep -q ',status,terminal,' "$game_file"; then
      echo "[$method] seed=$seed failed rc=$runner_rc"
      method_failed=1
      break
    fi
  done

  aggregate="$(awk -F, '
    /^game,/ {
      for(i=1;i<NF;i+=2) value[$i]=$(i+1)
      count++; score+=value["sco"]; big+=value["big"];
      moves+=value["moves"]; search+=value["search_time_us"]
      delete value
    }
    END {if(count) printf "%d %.3f %.3f %.3f %.0f\n",count,score/count,big/count,moves/count,search; else print "0 0 0 0 0"}
  ' "${game_files[@]}" 2>/dev/null || echo "0 0 0 0 0")"
  read -r completed_games avg_score avg_big avg_moves total_search <<< "$aggregate"
  if [[ "$method_failed" -eq 0 && "$completed_games" -eq 10 ]]; then
    method_status=completed
  else
    method_status=failed
    overall_failure=1
  fi
  echo "$method,$selected_depth,$calibration_avg,$calibration_status,$method_status,$completed_games,$avg_score,$avg_big,$avg_moves,$total_search" >> "$SUMMARY_CSV"
done

echo "comparison finished; summary=$SUMMARY_CSV"
exit "$overall_failure"
