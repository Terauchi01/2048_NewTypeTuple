#!/usr/bin/env bash

# Usage:
#   ./exp4-0.sh TUPLE ST
#
# Example:
#   ./exp4-0.sh 8 0
#   DATA_DIR=learn_double/count004_2e11 DATA_COUNT=004 ./exp4-0.sh 8 0
#
# TUPLE: 6, 7, 8, or 9
# ST:    expectimaxに渡す開始seedの接頭値（従来どおり末尾に0000を付ける）
#
# Environment:
#   DATA_DIR:   learned data directory (default: learn_result)
#   DATA_COUNT: file count suffix (default: 200)
#   SEEDS:      space-separated seeds (default: discover all matching files)
#   MAX_JOBS:   maximum simultaneous expectimax processes
#   DEPTHS:      space-separated depths to run (default: 1 2 3 4 5)
#   JOB_LOG_DIR: if set, also save each data-file/depth job to a separate log

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
data_dir=${DATA_DIR:-"${SCRIPT_DIR}/learn_result"}
data_count=${DATA_COUNT:-200}
start_seed="${ST}0000"

if [[ "$data_dir" != /* ]]; then
    data_dir="${SCRIPT_DIR}/${data_dir}"
fi

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

if [ -n "${JOB_LOG_DIR:-}" ]; then
    if [[ "$JOB_LOG_DIR" != /* ]]; then
        JOB_LOG_DIR="${SCRIPT_DIR}/${JOB_LOG_DIR}"
    fi
    mkdir -p "$JOB_LOG_DIR"
fi

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

selected_indices=()
for requested_depth in ${DEPTHS:-1 2 3 4 5}; do
    if [[ ! "$requested_depth" =~ ^[1-5]$ ]]; then
        echo "error: DEPTHS must contain values from 1 through 5: $requested_depth" >&2
        exit 2
    fi
    selected_indices+=("$((requested_depth - 1))")
done

if [ "${#selected_indices[@]}" -eq 0 ]; then
    echo "error: DEPTHS must not be empty" >&2
    exit 2
fi

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
    local input_file=$1
    shift

    local depth=${4}
    local input_name job_name job_log
    input_name=$(basename "$input_file")
    job_name=${input_name%.xz}
    job_name=${job_name%.dat}

    print_command "$@" "$input_file"

    if [ "$DRY_RUN" = "1" ]; then
        return
    fi

    while [ "$running_jobs" -ge "$MAX_JOBS" ]; do
        wait_for_one
    done

    if [ -n "${JOB_LOG_DIR:-}" ]; then
        job_log="${JOB_LOG_DIR}/${job_name}-depth${depth}.log"
        echo "job-log: $job_log"
        {
            printf 'input=%s depth=%s\n' "$input_file" "$depth"
            run_input "$input_file" "$@"
        } 2>&1 | tee "$job_log" &
    else
        run_input "$input_file" "$@" &
    fi
    running_jobs=$((running_jobs + 1))
}

run_input() {
    local input_file=$1
    shift

    if [[ "$input_file" != *.xz ]]; then
        "$@" "$input_file"
        return
    fi

    local fifo_dir fifo xz_pid status=0
    fifo_dir=$(mktemp -d "${TMPDIR:-/tmp}/exp4-0.XXXXXX")
    fifo="${fifo_dir}/$(basename "${input_file%.xz}")"
    mkfifo "$fifo"

    xz -dc -- "$input_file" > "$fifo" &
    xz_pid=$!

    "$@" "$fifo" || status=$?
    wait "$xz_pid" || status=$?
    rm -f -- "$fifo"
    rmdir -- "$fifo_dir"
    return "$status"
}

if [ ! -d "$data_dir" ]; then
    echo "error: data directory is missing: $data_dir" >&2
    exit 1
fi

dat_files=()
if [ -n "${SEEDS:-}" ]; then
    for seed in $SEEDS; do
        if [[ ! "$seed" =~ ^[0-9]+$ ]]; then
            echo "error: SEEDS must contain non-negative integers: $seed" >&2
            exit 2
        fi
        dat_file="${data_dir}/tuples${tuple}-seed${seed}-VSE-count${data_count}.dat"
        if [ -r "$dat_file" ]; then
            dat_files+=("$dat_file")
        elif [ -r "${dat_file}.xz" ]; then
            dat_files+=("${dat_file}.xz")
        else
            echo "error: learned data is missing: ${dat_file}[.xz]" >&2
            exit 1
        fi
    done
else
    shopt -s nullglob
    candidates=(
        "${data_dir}"/tuples"${tuple}"-seed*-VSE-count"${data_count}".dat
        "${data_dir}"/tuples"${tuple}"-seed*-VSE-count"${data_count}".dat.xz
    )
    shopt -u nullglob

    declare -A seen_inputs=()
    for candidate in "${candidates[@]}"; do
        input_key=${candidate%.xz}
        if [ -z "${seen_inputs[$input_key]+x}" ]; then
            dat_files+=("$candidate")
            seen_inputs[$input_key]=1
        fi
    done
fi

if [ "${#dat_files[@]}" -eq 0 ]; then
    echo "error: no learned data found: ${data_dir}/tuples${tuple}-seed*-VSE-count${data_count}.dat[.xz]" >&2
    exit 1
fi

for dat_file in "${dat_files[@]}"; do

    for index in "${selected_indices[@]}"; do
        run_job \
            "$dat_file" \
            "$runtime" \
            "$start_seed" \
            "${game_counts[$index]}" \
            "${depths[$index]}"
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
