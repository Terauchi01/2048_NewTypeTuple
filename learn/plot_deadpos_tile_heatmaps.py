#!/usr/bin/env python3
"""Plot heatmaps for tile-value position probabilities.

Reads CSV files produced by analyze_dead_boards_tilepos.py:
  tile_0002.csv, tile_0004.csv, ... (each 4x4)

Outputs a single PNG with a grid of heatmaps.

Usage:
  python3 plot_deadpos_tile_heatmaps.py --in-dir tilepos_log6 --out tilepos_log6_heatmaps.png

Optional:
  --tiles 2 4 8 16 32
  (if omitted, tiles are inferred from filenames in --in-dir)
"""

from __future__ import annotations

import argparse
import glob
import math
import os
from typing import Any, List, Sequence


def read_csv_4x4(path: str) -> List[List[float]]:
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


def _infer_tiles_from_dir(in_dir: str) -> List[int]:
    tiles: List[int] = []
    for path in glob.glob(os.path.join(in_dir, "tile_*.csv")):
        base = os.path.basename(path)
        # tile_0002.csv
        num = base[len("tile_") : -len(".csv")]
        try:
            tiles.append(int(num))
        except ValueError:
            continue
    return sorted(set(tiles))


def _flatten_axes(axes: Any) -> List[Any]:
    """Return a flat list of matplotlib Axes objects.

    plt.subplots can return:
    - a single Axes
    - a numpy.ndarray of Axes
    - (rarely) nested python lists
    This function flattens all of them safely.
    """
    if axes is None:
        return []
    if hasattr(axes, "ravel"):
        return list(axes.ravel().tolist())
    if hasattr(axes, "flat"):
        return list(axes.flat)
    if isinstance(axes, (list, tuple)):
        out: List[Any] = []
        for item in axes:
            out.extend(_flatten_axes(item))
        return out
    return [axes]


def main() -> None:
    ap = argparse.ArgumentParser(description="Plot tile-value position heatmaps")
    ap.add_argument("--in-dir", required=True, help="Directory containing tile_*.csv")
    ap.add_argument("--out", required=True, help="Output PNG path")
    ap.add_argument("--tiles", nargs="*", type=int, default=None, help="Tiles to plot (e.g., 2 4 8 16).")
    args = ap.parse_args()

    tiles = args.tiles if args.tiles is not None and len(args.tiles) > 0 else _infer_tiles_from_dir(args.in_dir)
    if not tiles:
        raise ValueError("No tiles to plot. Provide --tiles or ensure tile_*.csv exists in --in-dir.")

    arrays: List[List[List[float]]] = []
    for t in tiles:
        path = os.path.join(args.in_dir, f"tile_{t:04d}.csv")
        if not os.path.exists(path):
            raise FileNotFoundError(f"Missing input: {path}")
        arrays.append(read_csv_4x4(path))

    vmax = float(max(_max_in_matrix(a) for a in arrays))
    vmin = 0.0

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    n = len(tiles)
    cols = int(math.ceil(math.sqrt(n)))
    rows = int(math.ceil(n / cols))

    fig, axes = plt.subplots(rows, cols, figsize=(3.2 * cols, 3.2 * rows), constrained_layout=True)
    axes_list = _flatten_axes(axes)

    last_im = None
    for i, ax in enumerate(axes_list):
        if i >= n:
            ax.axis("off")
            continue
        t = tiles[i]
        data = arrays[i]
        last_im = ax.imshow(data, cmap="viridis", vmin=vmin, vmax=vmax, origin="upper")
        ax.set_title(f"tile {t}")
        ax.set_xticks(range(4))
        ax.set_yticks(range(4))
        ax.set_xticklabels([])
        ax.set_yticklabels([])

    if last_im is not None:
        cbar = fig.colorbar(last_im, ax=axes_list, shrink=0.9)
        cbar.set_label("P(cell == tile)")

    out_path = args.out
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    fig.savefig(out_path, dpi=200)
    print(out_path)


if __name__ == "__main__":
    main()
