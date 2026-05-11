#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   cd learn
#   ./run.sh [IN_DIR] [OUT_PNG]
# Example:
#   ./run.sh deadpos_log6 deadpos_log6_heatmaps.png

HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"

IN_DIR="${1:-deadpos_log6}"
OUT_PNG="${2:-deadpos_log6_heatmaps.png}"

if [[ ! -f "plot_deadpos_heatmaps.py" ]]; then
  echo "ERROR: plot_deadpos_heatmaps.py not found in $HERE" >&2
  exit 1
fi

if [[ ! -d "$IN_DIR" ]]; then
  echo "ERROR: input dir not found: $IN_DIR" >&2
  echo "Hint: run from learn/ or pass a correct path." >&2
  exit 1
fi

VENV_DIR=".venv"
if [[ ! -d "$VENV_DIR" ]]; then
  python3 -m venv "$VENV_DIR"
fi

# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"

python3 -m pip install -q -U pip
python3 -m pip install -q matplotlib

python3 plot_deadpos_heatmaps.py --in-dir "$IN_DIR" --out "$OUT_PNG"
