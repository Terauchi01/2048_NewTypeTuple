#!/usr/bin/env python3
"""Analyze terminal (game-over) 2048 boards: tile-value position probabilities.

For each tile value v (2,4,8,16,...) this script computes a 4x4 grid of:
  P(cell == v)
across all game-over boards found in the input logs.

Input format
------------
Same as analyze_dead_boards.py; lines like:
- board,game,<game_id>,<16 cells...>
- board_restart,game,<game_id>,restart_i,<r>,turn,<t>,<16 cells...>

The <16 cells...> are tile values (0,2,4,8,...) in row-major order.

Output
------
- Prints summary and per-tile grids.
- Optionally writes CSV files under --out-dir:
    tile_0002.csv, tile_0004.csv, ...

Usage
-----
python3 learn/analyze_dead_boards_tilepos.py --inputs log6 --out-dir tilepos_log6
python3 learn/analyze_dead_boards_tilepos.py --inputs logs/*.log --tiles 2 4 8 16
"""

from __future__ import annotations

import argparse
import glob
import os
from dataclasses import dataclass
from typing import Dict, Iterable, List, Sequence, Tuple


BOARD_SIZE = 4
NCELLS = BOARD_SIZE * BOARD_SIZE


@dataclass(frozen=True)
class ParsedBoard:
    source: str
    line_no: int
    cells: Tuple[int, ...]  # length 16


def _iter_input_paths(patterns: Sequence[str]) -> List[str]:
    paths: List[str] = []
    for pat in patterns:
        expanded = glob.glob(pat)
        if expanded:
            paths.extend(expanded)
        else:
            paths.append(pat)
    seen = set()
    unique: List[str] = []
    for p in paths:
        if p in seen:
            continue
        seen.add(p)
        unique.append(p)
    return unique


def _try_parse_board_line(csv_line: str) -> Tuple[bool, Tuple[int, ...] | None]:
    if not (csv_line.startswith("board,") or csv_line.startswith("board_restart,")):
        return False, None
    parts = [p.strip() for p in csv_line.rstrip("\n").split(",")]
    if len(parts) < 1 + 2 + NCELLS:
        return False, None
    tail = parts[-NCELLS:]
    try:
        cells = tuple(int(x) for x in tail)
    except ValueError:
        return False, None
    if len(cells) != NCELLS:
        return False, None
    return True, cells


def read_boards(paths: Sequence[str]) -> List[ParsedBoard]:
    boards: List[ParsedBoard] = []
    for path in paths:
        if not os.path.exists(path):
            raise FileNotFoundError(f"Input not found: {path}")
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for idx, line in enumerate(f, start=1):
                ok, cells = _try_parse_board_line(line)
                if not ok or cells is None:
                    continue
                boards.append(ParsedBoard(source=path, line_no=idx, cells=cells))
    return boards


def _is_power_of_two(x: int) -> bool:
    return x > 0 and (x & (x - 1)) == 0


def _format_grid(values: Sequence[float]) -> str:
    lines: List[str] = []
    for y in range(BOARD_SIZE):
        row = values[y * BOARD_SIZE : (y + 1) * BOARD_SIZE]
        lines.append(" ".join(f"{v:7.4f}" for v in row))
    return "\n".join(lines)


def write_csv_grid(path: str, values: Sequence[float]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        for y in range(BOARD_SIZE):
            row = values[y * BOARD_SIZE : (y + 1) * BOARD_SIZE]
            f.write(",".join(f"{v:.10f}" for v in row) + "\n")


def analyze_tile_position_probs(
    boards: Sequence[ParsedBoard],
    tiles: Sequence[int],
) -> Dict[int, List[float]]:
    if not boards:
        raise ValueError("No boards found in inputs (no 'board,' lines matched).")
    if not tiles:
        raise ValueError("No tiles specified.")

    acc: Dict[int, List[float]] = {t: [0.0 for _ in range(NCELLS)] for t in tiles}

    for b in boards:
        cells = b.cells
        for pos, v in enumerate(cells):
            if v in acc:
                acc[v][pos] += 1.0

    n = float(len(boards))
    for t in tiles:
        for pos in range(NCELLS):
            acc[t][pos] /= n

    return acc


def main() -> None:
    ap = argparse.ArgumentParser(description="Analyze game-over boards: tile-value position probabilities")
    ap.add_argument("--inputs", nargs="+", required=True, help="Input log paths or glob patterns")
    ap.add_argument(
        "--tiles",
        nargs="*",
        type=int,
        default=None,
        help="Tile values to analyze (e.g., 2 4 8 16). If omitted, auto-detect from data.",
    )
    ap.add_argument("--out-dir", default=None, help="If set, writes tile_*.csv under this directory")
    ap.add_argument("--max-boards", type=int, default=None, help="Optional cap for debugging")
    args = ap.parse_args()

    paths = _iter_input_paths(args.inputs)
    boards = read_boards(paths)
    if args.max_boards is not None:
        boards = boards[: args.max_boards]

    if args.tiles is None:
        values = set()
        for b in boards:
            for v in b.cells:
                if v == 0:
                    continue
                if _is_power_of_two(v):
                    values.add(v)
        tiles = sorted(values)
    else:
        tiles = [t for t in args.tiles if t > 0]
        for t in tiles:
            if not _is_power_of_two(t):
                raise ValueError(f"Tile must be a power of two: {t}")
        tiles = sorted(set(tiles))

    acc = analyze_tile_position_probs(boards, tiles)

    print(f"boards={len(boards)}")
    print(f"tiles={tiles}")

    for t in tiles:
        print(f"\ntile={t}")
        print(_format_grid(acc[t]))

    if args.out_dir:
        for t in tiles:
            out_path = os.path.join(args.out_dir, f"tile_{t:04d}.csv")
            write_csv_grid(out_path, acc[t])
        print(f"\nwrote_csv_dir={args.out_dir}")


if __name__ == "__main__":
    main()
