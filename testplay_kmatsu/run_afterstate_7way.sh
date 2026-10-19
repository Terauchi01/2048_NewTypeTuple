#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"

if [[ $# -gt 1 ]]; then
  echo "Usage: $0 [seed0-6tuple-evaluator.dat[.xz]]" >&2
  exit 2
fi

if [[ $# -eq 1 ]]; then
  EVFILE="$1"
elif [[ -n "${EVFILE:-}" ]]; then
  EVFILE="$EVFILE"
elif [[ -r "$REPO_ROOT/tuples6-seed0-VSE-count004.dat" ]]; then
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

RUN_DIR="${RUN_DIR:-$SCRIPT_DIR/results/afterstate_7way_$TIMESTAMP}"
mkdir -p "$RUN_DIR"
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"

if [[ "${SKIP_BUILD:-0}" != 1 ]]; then
  make -C "$SCRIPT_DIR" build-beam-afterstate-runner-6
fi
BEAM_BIN="$SCRIPT_DIR/test_beam_search_afterstate_6" \
SKIP_BUILD=1 RUN_DIR="$RUN_DIR" \
  "$SCRIPT_DIR/run_beam_comparison.sh" "$EVFILE"
