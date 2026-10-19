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

DEPTH="${DEPTH:-5}"
WIDTH="${WIDTH:-256}"
REPEATS="${REPEATS:-5}"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
RESULT_DIR="${RESULT_DIR:-$SCRIPT_DIR/results/profile_beam_$TIMESTAMP}"
mkdir -p "$RESULT_DIR"
RESULT_DIR="$(cd "$RESULT_DIR" && pwd -P)"

CXX="${CXX:-clang++}"
"$CXX" -pthread -std=c++20 -W -Wall -O3 -I"$REPO_ROOT/head" \
  "$SCRIPT_DIR/profile_beam_search.cpp" \
  "$SCRIPT_DIR/beam_search.cpp" "$SCRIPT_DIR/beam_search_fast.cpp" \
  "$SCRIPT_DIR/double_evaluator.cpp" "$SCRIPT_DIR/game2048.cpp" \
  -o "$SCRIPT_DIR/beam_search_profile"

"$SCRIPT_DIR/beam_search_profile" "$EVFILE" "$DEPTH" "$WIDTH" "$REPEATS" \
  | tee "$RESULT_DIR/profile.csv"
echo "RESULT_DIR=$RESULT_DIR"
