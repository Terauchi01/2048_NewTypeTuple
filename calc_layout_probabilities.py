#!/usr/bin/env python3
"""
各layoutについて128作成確率を計算するスクリプト。
seed_top8_layout_frequencies.tsv から一意layoutを抽出して、
exact_second_32768で確率を計算する。

使用方法:
  python3 calc_layout_probabilities.py --input seed_top8_layout_frequencies.tsv \
                                        --output layout_probabilities.tsv \
                                        --exact-bin ./exact_second_32768 \
                                        --job-id 0 --total-jobs 16
"""

import argparse
import subprocess
import sys
import os
from pathlib import Path
from typing import Dict, List, Tuple

def read_unique_layouts(input_file: str) -> List[Tuple[str, str, str]]:
    """
    layout頻度ファイルから一意layoutを抽出。
    (classification, layout_id, rank_cells) の重複排除リストを返す
    """
    layouts = {}
    with open(input_file, 'r') as f:
        header = f.readline()  # skip header
        for line in f:
            parts = line.strip().split('\t')
            if len(parts) < 4:
                continue
            classification = parts[1]
            layout_id = parts[2]
            rank_cells = parts[3]
            
            key = (classification, layout_id)
            if key not in layouts:
                layouts[key] = rank_cells
    
    return [(cls, lid, rank_cells) for (cls, lid), rank_cells in layouts.items()]

def compute_layout_probability(exact_bin: str, classification: str, rank_cells: str) -> Tuple[bool, float, str]:
    """
    exact_second_32768 を使用して、layoutの128作成確率を計算。
    
    Returns:
      (success: bool, probability: float, error_msg: str)
    """
    try:
        cmd = [
            exact_bin,
            f"--layout={classification}",
            f"--protected-rank-cells={rank_cells}",
            "--target-exponent=15",
            "--mode=enumerate"
        ]
        
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300
        )
        
        if result.returncode != 0:
            return False, 0.0, f"exit code {result.returncode}"
        
        # 出力から possible: true/false を抽出
        for line in result.stdout.split('\n'):
            if line.startswith('possible='):
                possible = line.split('=')[1].strip().lower() == 'true'
                return True, 1.0 if possible else 0.0, ""
        
        return False, 0.0, "possible field not found in output"
    
    except subprocess.TimeoutExpired:
        return False, 0.0, "timeout"
    except Exception as e:
        return False, 0.0, str(e)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, help='seed_top8_layout_frequencies.tsv')
    parser.add_argument('--output', required=True, help='出力ファイル (layout_probabilities.tsv)')
    parser.add_argument('--exact-bin', default='./exact_second_32768', 
                       help='exact_second_32768 バイナリパス')
    parser.add_argument('--job-id', type=int, default=0, help='並列ジョブID (0-based)')
    parser.add_argument('--total-jobs', type=int, default=1, help='総ジョブ数')
    args = parser.parse_args()
    
    # 一意layoutを抽出
    layouts = read_unique_layouts(args.input)
    print(f"Total unique layouts: {len(layouts)}", file=sys.stderr)
    
    # ジョブIDで分割（ラウンドロビン）
    job_layouts = [layouts[i] for i in range(len(layouts)) if i % args.total_jobs == args.job_id]
    print(f"Job {args.job_id}: {len(job_layouts)} layouts to process", file=sys.stderr)
    
    # 確率を計算
    results = []
    for idx, (classification, layout_id, rank_cells) in enumerate(job_layouts):
        if idx % max(1, len(job_layouts) // 10) == 0:
            print(f"Job {args.job_id}: {idx}/{len(job_layouts)}", file=sys.stderr)
        
        success, prob, error_msg = compute_layout_probability(args.exact_bin, classification, rank_cells)
        
        if success:
            results.append((classification, layout_id, rank_cells, prob))
        else:
            print(f"Error computing {classification}/{layout_id}: {error_msg}", file=sys.stderr)
            # エラーでも0.0で記録（後で手動確認可能）
            results.append((classification, layout_id, rank_cells, 0.0))
    
    # 結果を出力
    with open(args.output, 'w') as f:
        f.write("classification\tlayout_id\trank_cells\tprobability_128\n")
        for classification, layout_id, rank_cells, prob in results:
            f.write(f"{classification}\t{layout_id}\t{rank_cells}\t{prob}\n")
    
    print(f"Job {args.job_id}: Wrote {len(results)} results to {args.output}", file=sys.stderr)

if __name__ == '__main__':
    main()
