#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
EVFILE="${1:-$REPO_ROOT/tuples6-seed0-VSE-count004.dat}"
if [[ "$EVFILE" != /* ]]; then
  EVFILE="$(cd "$(dirname "$EVFILE")" && pwd -P)/$(basename "$EVFILE")"
fi
if [[ ! -r "$EVFILE" ]]; then
  echo "Evaluator file is not readable: $EVFILE" >&2
  exit 2
fi

GAME_COUNT="${GAME_COUNT:-10}"
DEPTH="${DEPTH:-5}"
MAX_MOVES="${MAX_MOVES:-20}"
METHOD="${METHOD:-fixed}"
MAX_WIDTH="${MAX_WIDTH:-256}"
K="${K:-128}"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/beam_fast_$TIMESTAMP}"
mkdir -p "$RESULT_DIR"
RESULT_DIR="$(cd "$RESULT_DIR" && pwd -P)"

make -C "$SCRIPT_DIR" beam
bash "$SCRIPT_DIR/build_beam_fast.sh"

COMMON=(0 "$GAME_COUNT" "$DEPTH" "$METHOD" "$MAX_WIDTH" "$EVFILE"
  --threads 1 --max-moves "$MAX_MOVES" --timeout-sec 60 --k "$K")
"$SCRIPT_DIR/test_beam_search_6" "${COMMON[@]}" \
  | tee "$RESULT_DIR/baseline.csv"
"$SCRIPT_DIR/test_beam_search_fast" "${COMMON[@]}" \
  | tee "$RESULT_DIR/fast.csv"

awk -F, '
  FNR==1 {label=FILENAME; sub(/^.*\//,"",label); total[label]=0; moves[label]=0; games[label]=0}
  /^game,/ {
    name=FILENAME; sub(/^.*\//,"",name)
    for(i=1;i<NF;i+=2) value[$i]=$(i+1)
    total[name]+=value["search_time_us"]
    moves[name]+=value["moves"]
    games[name]++
    delete value
  }
  END {
    print "implementation,games,total_search_time_us,avg_search_us_per_move"
    for(name in total) {
      avg=moves[name] ? total[name]/moves[name] : 0
      printf "%s,%d,%.0f,%.3f\n",name,games[name],total[name],avg
    }
  }
' "$RESULT_DIR/baseline.csv" "$RESULT_DIR/fast.csv" \
  | tee "$RESULT_DIR/summary.csv"
echo "RESULT_DIR=$RESULT_DIR"
