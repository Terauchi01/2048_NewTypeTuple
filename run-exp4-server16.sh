#!/usr/bin/env bash

# 16-thread / 128-GiB server: tuple 8, then tuple 7
# Usage: ./run-exp4-server16.sh [ST]

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
echo "building parallel tuple 8 and 7 executables"
make -C testplay_double build-8-parallel build-7-parallel

run_depth() {
    local tuple=$1
    local depth=$2
    local max_jobs=$3
    local tuple_log="${LOG_DIR}/tuple${tuple}.log"
    local runtime="${SCRIPT_DIR}/testplay_double/test_expectimax_parallel_${tuple}"

    echo "starting tuple ${tuple} depth ${depth} with MAX_JOBS=${max_jobs}" | tee -a "$tuple_log"
    DATA_DIR="$DATA_DIR" DATA_COUNT="$DATA_COUNT" DEPTHS="$depth" \
        MAX_JOBS="$max_jobs" RUNTIME="$runtime" \
        JOB_LOG_DIR="${JOB_LOG_ROOT}/tuple${tuple}" \
        ./exp4-0.sh "$tuple" "$ST" 2>&1 | tee -a "$tuple_log"
}

# Run the expensive depth first. Depth 5 uses up to four threads per job.
: > "${LOG_DIR}/tuple8.log"
run_depth 8 5 4
run_depth 8 1 10

: > "${LOG_DIR}/tuple7.log"
run_depth 7 5 4
run_depth 7 1 5

echo "server16 assignment completed"
