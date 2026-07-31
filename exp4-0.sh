#!/usr/bin/env bash

# Usage:
#   ./exp4-0.sh TUPLE ST
#
# Example:
#   ./exp4-0.sh 8 0
#
# TUPLE: 6, 7, 8, or 9
# ST:    expectimaxに渡す開始seedの接頭値（従来どおり末尾に0000を付ける）

set -u
set -o pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 TUPLE ST" >&2
    echo "  TUPLE: 6, 7, 8, or 9" >&2
    echo "  example: $0 8 0" >&2
    exit 2
fi

tuple=$1
ST=$2

case "$tuple" in
    6|7|8|9)
        ;;
    *)
        echo "error: TUPLE must be one of 6, 7, 8, or 9: $tuple" >&2
        exit 2
        ;;
esac

if [[ ! "$ST" =~ ^[0-9]+$ ]]; then
    echo "error: ST must be a non-negative integer: $ST" >&2
    exit 2
fi

SCRIPT_DIR=$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
    pwd
)

runtime="${SCRIPT_DIR}/testplay_double/test_expectimax_${tuple}"
data_dir="${SCRIPT_DIR}/learn_result"
start_seed="${ST}0000"

# MAX_JOBSが環境変数で指定されていない場合、
# タプル数に応じて同時実行数を設定する
if [ -z "${MAX_JOBS+x}" ]; then
    case "$tuple" in
        6)
            MAX_JOBS=5
            ;;
        7)
            MAX_JOBS=5
            ;;
        8)
            MAX_JOBS=3
            ;;
        9)
            MAX_JOBS=2
            ;;
    esac
fi

DRY_RUN=${DRY_RUN:-0}

if [[ ! "$MAX_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: MAX_JOBS must be a positive integer: $MAX_JOBS" >&2
    exit 2
fi

if [ ! -x "$runtime" ]; then
    echo "error: expectimax executable is missing or not executable: $runtime" >&2
    echo "build it with: make -C \"${SCRIPT_DIR}/testplay_double\" build-${tuple}" >&2
    exit 1
fi

depths=(1 2 3 4 5)
game_counts=(10000 3000 1000 300 100)

running_jobs=0
failed_jobs=0

print_command() {
    printf 'run:'
    printf ' %q' "$@"
    printf '\n'
}

wait_for_one() {
    if ! wait -n; then
        failed_jobs=$((failed_jobs + 1))
    fi
    running_jobs=$((running_jobs - 1))
}

run_job() {
    print_command "$@"

    if [ "$DRY_RUN" = "1" ]; then
        return
    fi

    while [ "$running_jobs" -ge "$MAX_JOBS" ]; do
        wait_for_one
    done

    "$@" &
    running_jobs=$((running_jobs + 1))
}

for seed in {0..4}; do
    dat_file="${data_dir}/tuples${tuple}-seed${seed}-VSE-count200.dat"

    if [ ! -r "$dat_file" ]; then
        echo "error: learned dat file is missing or unreadable: $dat_file" >&2
        exit 1
    fi
done

for seed in {0..4}; do
    dat_file="${data_dir}/tuples${tuple}-seed${seed}-VSE-count200.dat"

    for index in "${!depths[@]}"; do
        run_job \
            "$runtime" \
            "$start_seed" \
            "${game_counts[$index]}" \
            "${depths[$index]}" \
            "$dat_file"
    done
done

while [ "$running_jobs" -gt 0 ]; do
    wait_for_one
done

if [ "$failed_jobs" -ne 0 ]; then
    echo "error: ${failed_jobs} expectimax job(s) failed" >&2
    exit 1
fi

if [ "$DRY_RUN" = "1" ]; then
    echo "dry run completed"
else
    echo "all done"
fi
