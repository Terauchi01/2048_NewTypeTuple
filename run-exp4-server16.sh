#!/usr/bin/env bash

# 16-thread / 128-GiB server: tuple 9 and 6
# Usage: ./run-exp4-server16.sh [ST]

set -euo pipefail
set -o pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ST=${1:-0}
DATA_DIR=${DATA_DIR:-learn_double/count004_2e11}
DATA_COUNT=${DATA_COUNT:-004}
LOG_DIR=${LOG_DIR:-"${SCRIPT_DIR}/exp4_logs"}
MAX_JOBS_6=${MAX_JOBS_6:-8}
MAX_JOBS_9=${MAX_JOBS_9:-5}
DEPTHS="1 5"

mkdir -p "$LOG_DIR"
cd "$SCRIPT_DIR"

echo "host=$(hostname) threads=$(nproc) assignment=tuples9,6"
echo "building tuple 9 and 6 executables"
make -C testplay_double build-9 build-6

echo "starting tuple 9 with MAX_JOBS=${MAX_JOBS_9}"
DATA_DIR="$DATA_DIR" DATA_COUNT="$DATA_COUNT" DEPTHS="$DEPTHS" \
    MAX_JOBS="$MAX_JOBS_9" JOB_LOG_DIR="${LOG_DIR}/jobs/tuple9" \
    ./exp4-0.sh 9 "$ST" 2>&1 | tee "${LOG_DIR}/tuple9.log"

echo "starting tuple 6 with MAX_JOBS=${MAX_JOBS_6}"
DATA_DIR="$DATA_DIR" DATA_COUNT="$DATA_COUNT" DEPTHS="$DEPTHS" \
    MAX_JOBS="$MAX_JOBS_6" JOB_LOG_DIR="${LOG_DIR}/jobs/tuple6" \
    ./exp4-0.sh 6 "$ST" 2>&1 | tee "${LOG_DIR}/tuple6.log"

echo "server16 assignment completed"
