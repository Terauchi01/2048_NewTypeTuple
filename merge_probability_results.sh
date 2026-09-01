#!/bin/bash
# 並列計算結果の統合スクリプト
# 使用方法: bash merge_probability_results.sh probability_results

OUTPUT_DIR="${1:-.}"
MERGED_FILE="${OUTPUT_DIR}/layout_probabilities_merged.tsv"
FINAL_RESULTS="${OUTPUT_DIR}/seed_weighted_average_128_probability.tsv"

echo "Merging results from ${OUTPUT_DIR}..."

# ヘッダを出力
echo "classification	layout_id	rank_cells	probability_128" > "${MERGED_FILE}"

# 全jobのファイルをマージ
for file in "${OUTPUT_DIR}"/job_*.tsv; do
    if [ -f "${file}" ]; then
        tail -n +2 "${file}" >> "${MERGED_FILE}"
    fi
done

echo "Merged results written to: ${MERGED_FILE}"
echo "Total layouts: $(tail -n +2 "${MERGED_FILE}" | wc -l)"

# Pythonで加重平均を計算
python3 << 'EOF'
import sys
from pathlib import Path
from collections import defaultdict

# オプション: output_dirを引数から取得
output_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('.')
merged_file = output_dir / 'layout_probabilities_merged.tsv'
freq_file = Path('.') / 'seed_top8_layout_frequencies.tsv'
output_file = output_dir / 'seed_weighted_average_128_probability.tsv'

if not merged_file.exists():
    print(f"Error: {merged_file} not found", file=sys.stderr)
    sys.exit(1)

if not freq_file.exists():
    print(f"Error: {freq_file} not found", file=sys.stderr)
    sys.exit(1)

# 確率データを読み込み
probabilities = {}
with open(merged_file) as f:
    f.readline()  # skip header
    for line in f:
        parts = line.strip().split('\t')
        if len(parts) < 4:
            continue
        classification, layout_id, rank_cells, prob = parts[0], parts[1], parts[2], float(parts[3])
        key = (classification, layout_id)
        probabilities[key] = prob

# 頻度データを読み込み
frequencies = defaultdict(lambda: defaultdict(lambda: []))  # seed -> classification -> [(freq, prob)]
seed_totals = {}

with open(freq_file) as f:
    f.readline()  # skip header
    for line in f:
        parts = line.strip().split('\t')
        if len(parts) < 7:
            continue
        seed = int(parts[0])
        classification = parts[1]
        layout_id = parts[2]
        frequency = float(parts[6])
        
        key = (classification, layout_id)
        prob = probabilities.get(key, 0.0)
        
        frequencies[seed][classification].append((frequency, prob))

# Seed別・分類別の加重平均を計算
results = []
seed_results = defaultdict(dict)

for seed in sorted(frequencies.keys()):
    for classification in sorted(frequencies[seed].keys()):
        weighted_sum = sum(freq * prob for freq, prob in frequencies[seed][classification])
        results.append((seed, classification, weighted_sum))
        seed_results[seed][classification] = weighted_sum

# 結果を出力
print(f"\n=== Seed-wise 128 Creation Probability ===\n")
with open(output_file, 'w') as f:
    f.write("seed\tclassification\tprobability_128\n")
    for seed, classification, prob in results:
        f.write(f"{seed}\t{classification}\t{prob}\n")
        print(f"seed {seed:2d}: {classification:12s} = {prob:.4%}")

# 分類別平均を計算
print(f"\n=== Classification Average ===\n")
straight_seeds = [0, 2, 4, 7, 8, 9]
block2x2_seeds = [1, 3, 5, 6]

straight_avg = sum(seed_results[s].get('straight', 0.0) for s in straight_seeds) / len(straight_seeds)
block2x2_avg = sum(seed_results[s].get('block2x2', 0.0) for s in block2x2_seeds) / len(block2x2_seeds)

print(f"Straight  (seeds {straight_seeds}): {straight_avg:.4%}")
print(f"Block2x2  (seeds {block2x2_seeds}): {block2x2_avg:.4%}")
print(f"Difference: {abs(straight_avg - block2x2_avg):.4%}")
print(f"Ratio: {straight_avg / block2x2_avg:.4f}x")

# 最終結果をファイルに追記
with open(output_file, 'a') as f:
    f.write(f"\n# Classification averages:\n")
    f.write(f"# Straight (0,2,4,7,8,9): {straight_avg:.10f}\n")
    f.write(f"# Block2x2 (1,3,5,6): {block2x2_avg:.10f}\n")
    f.write(f"# Difference: {abs(straight_avg - block2x2_avg):.10f}\n")
    f.write(f"# Ratio (straight/block2x2): {straight_avg / block2x2_avg:.10f}\n")

print(f"\nResults written to: {output_file}")
EOF
