#!/usr/bin/env bash

# 32-thread / 128-GiB server: tuple 8 and 7
# Usage: ./run-exp4-server32.sh [ST]

set -euo pipefail
set -o pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ST=${1:-0}
DATA_DIR=${DATA_DIR:-learn_double/count004_2e11}
DATA_COUNT=${DATA_COUNT:-004}
LOG_DIR=${LOG_DIR:-"${SCRIPT_DIR}/exp4_logs"}
JOB_LOG_ROOT=${JOB_LOG_ROOT:-"${LOG_DIR}/jobs"}

mkdir -p "$LOG_DIR" "$JOB_LOG_ROOT"
cd "$SCRIPT_DIR"

echo "host=$(hostname) threads=$(nproc) assignment=tuples8,7"
echo "building tuple 8 and 7 executables"
make -C testplay_double build-8 build-7

# Keep additional headroom for expectimax working data.
echo "starting tuple 8 with MAX_JOBS=8"
DATA_DIR="$DATA_DIR" DATA_COUNT="$DATA_COUNT" DEPTHS="1 5" MAX_JOBS=8 \
    JOB_LOG_DIR="${JOB_LOG_ROOT}/tuple8" \
    ./exp4-0.sh 8 "$ST" 2>&1 | tee "${LOG_DIR}/tuple8.log"

echo "starting tuple 7 with MAX_JOBS=8"
DATA_DIR="$DATA_DIR" DATA_COUNT="$DATA_COUNT" DEPTHS="1 5" MAX_JOBS=8 \
    JOB_LOG_DIR="${JOB_LOG_ROOT}/tuple7" \
    ./exp4-0.sh 7 "$ST" 2>&1 | tee "${LOG_DIR}/tuple7.log"

echo "server32 assignment completed"
