#!/bin/bash
# 並列実行スクリプト: 16並列で確率計算を実行
# 使用方法: bash run_parallel_probability.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT_FILE="${SCRIPT_DIR}/seed_top8_layout_frequencies.tsv"
EXACT_BIN="${SCRIPT_DIR}/exact_second_32768"
OUTPUT_DIR="${SCRIPT_DIR}/probability_results"
PYTHON_SCRIPT="${SCRIPT_DIR}/calc_layout_probabilities.py"

NUM_JOBS=16

# 出力ディレクトリ作成
mkdir -p "${OUTPUT_DIR}"

if [ ! -f "${INPUT_FILE}" ]; then
    echo "Error: ${INPUT_FILE} not found"
    exit 1
fi

if [ ! -f "${EXACT_BIN}" ]; then
    echo "Error: ${EXACT_BIN} not found"
    exit 1
fi

echo "Starting parallel probability calculation..."
echo "Jobs: ${NUM_JOBS}"
echo "Input: ${INPUT_FILE}"
echo "Output directory: ${OUTPUT_DIR}"

# 16並列で実行
pids=()
for job_id in $(seq 0 $((NUM_JOBS - 1))); do
    output_file="${OUTPUT_DIR}/job_${job_id}.tsv"
    python3 "${PYTHON_SCRIPT}" \
        --input "${INPUT_FILE}" \
        --output "${output_file}" \
        --exact-bin "${EXACT_BIN}" \
        --job-id "${job_id}" \
        --total-jobs "${NUM_JOBS}" &
    pids+=($!)
    echo "Started job ${job_id} (PID $!)"
done

# 全ジョブの完了を待機
echo "Waiting for all jobs to complete..."
for pid in "${pids[@]}"; do
    if wait $pid; then
        echo "Job PID $pid completed successfully"
    else
        echo "Job PID $pid failed with exit code $?"
    fi
done

echo "All parallel jobs completed."
echo "Results in: ${OUTPUT_DIR}"

# 結果の統合
echo "Merging results..."
bash "${SCRIPT_DIR}/merge_probability_results.sh" "${OUTPUT_DIR}"
