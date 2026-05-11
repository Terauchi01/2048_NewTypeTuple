#!/usr/bin/env python3
"""Plot heatmaps for dead-board rank position probabilities.

Reads CSV files produced by analyze_dead_boards.py:
  rank_01.csv ... rank_16.csv (each 4x4)

Outputs a single PNG with a 4x4 grid of heatmaps (rank 1..16).

Usage:
  python3 plot_deadpos_heatmaps.py --in-dir deadpos_log6 --out deadpos_log6_heatmaps.png
"""

from __future__ import annotations

import argparse
import os
from typing import List, Sequence


def read_rank_csv(path: str) -> List[List[float]]:
    rows: List[List[float]] = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rows.append([float(x) for x in line.split(",")])
    if len(rows) != 4 or any(len(r) != 4 for r in rows):
        shape = (len(rows), len(rows[0]) if rows else 0)
        raise ValueError(f"Expected 4x4 CSV at {path}, got shape={shape}")
    return rows


def _max_in_matrix(m: Sequence[Sequence[float]]) -> float:
    return max(max(row) for row in m)


def main() -> None:
    ap = argparse.ArgumentParser(description="Plot 2048 dead-board rank position heatmaps")
    ap.add_argument("--in-dir", required=True, help="Directory containing rank_01.csv ... rank_16.csv")
    ap.add_argument("--out", required=True, help="Output PNG path")
    args = ap.parse_args()

    in_dir = args.in_dir
    arrays: List[List[List[float]]] = []
    for rank in range(1, 17):
        path = os.path.join(in_dir, f"rank_{rank:02d}.csv")
        if not os.path.exists(path):
            raise FileNotFoundError(f"Missing input: {path}")
        arrays.append(read_rank_csv(path))

    vmax = float(max(_max_in_matrix(a) for a in arrays))
    vmin = 0.0

    # Import matplotlib lazily (so the script can still be imported without it)
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(4, 4, figsize=(12, 12), constrained_layout=True)

    last_im = None
    for i, ax in enumerate(axes.flat):
        rank = i + 1
        data = arrays[i]
        last_im = ax.imshow(data, cmap="viridis", vmin=vmin, vmax=vmax, origin="upper")
        ax.set_title(f"rank {rank}")
        ax.set_xticks(range(4))
        ax.set_yticks(range(4))
        ax.set_xticklabels([])
        ax.set_yticklabels([])

    if last_im is not None:
        cbar = fig.colorbar(last_im, ax=axes, shrink=0.9)
        cbar.set_label("Probability")

    out_path = args.out
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    fig.savefig(out_path, dpi=200)
    print(out_path)


if __name__ == "__main__":
    main()
