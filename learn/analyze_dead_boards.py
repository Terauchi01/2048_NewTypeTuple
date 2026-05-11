#!/usr/bin/env python3
"""Analyze terminal (game-over) 2048 boards from training logs.

This script aggregates, for each rank k=1..16 (largest -> 16th largest),
the probability that the k-th largest tile is located at each of the 16
board positions.

Input format
------------
It expects lines like either of the following (CSV):

- board,game,<game_id>,<16 cells...>
- board_restart,game,<game_id>,restart_i,<r>,turn,<t>,<16 cells...>

Where <16 cells...> are tile values (0,2,4,8,...) in row-major order.

Ties
-----
If multiple cells share the same value, ranks are ambiguous. We handle this
by distributing probability mass uniformly across tied positions for each
rank covered by that tie group.

Example: values [8,8,4,...]. The top-2 ranks are tied among the two 8-cells.
For rank=1 and rank=2, each of the two positions receives weight 1/2.

Outputs
-------
- Prints per-rank 4x4 probability grids.
- Optionally writes CSV files under --out-dir.

Usage
-----
python learn/analyze_dead_boards.py --inputs logs/*.log
python learn/analyze_dead_boards.py --inputs my.log --out-dir out
"""

from __future__ import annotations

import argparse
import glob
import os
from dataclasses import dataclass
from typing import Iterable, List, Sequence, Tuple


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
            # If the pattern is an exact path that doesn't exist, keep it for error message.
            paths.append(pat)
    # Deduplicate while preserving order
    seen = set()
    unique: List[str] = []
    for p in paths:
        if p in seen:
            continue
        seen.add(p)
        unique.append(p)
    return unique


def _try_parse_board_line(csv_line: str) -> Tuple[bool, Tuple[int, ...] | None]:
    # Fast path checks
    if not (csv_line.startswith("board,") or csv_line.startswith("board_restart,")):
        return False, None

    parts = [p.strip() for p in csv_line.rstrip("\n").split(",")]
    if len(parts) < 1 + 2 + NCELLS:  # minimal
        return False, None

    # Cells are the last 16 columns for both formats.
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
                if len(cells) != NCELLS:
                    continue
                boards.append(ParsedBoard(source=path, line_no=idx, cells=cells))
    return boards


def _rank_position_weights(cells: Sequence[int]) -> List[List[Tuple[int, float]]]:
    """For each rank r=0..15 (0=largest), return list of (pos, weight).

    Weights sum to 1.0 for each rank.
    """
    if len(cells) != NCELLS:
        raise ValueError(f"Expected {NCELLS} cells, got {len(cells)}")

    # Group positions by value
    value_to_positions: dict[int, List[int]] = {}
    for pos, v in enumerate(cells):
        value_to_positions.setdefault(v, []).append(pos)

    # Sort values descending
    sorted_values = sorted(value_to_positions.keys(), reverse=True)

    per_rank: List[List[Tuple[int, float]]] = [[] for _ in range(NCELLS)]

    rank_cursor = 0
    for v in sorted_values:
        positions = value_to_positions[v]
        group_size = len(positions)
        if group_size <= 0:
            continue
        start = rank_cursor
        end = min(NCELLS, rank_cursor + group_size)  # exclusive
        if start >= NCELLS:
            break
        # For any rank within [start, end), distribute uniformly among tied positions
        w = 1.0 / group_size
        for r in range(start, end):
            per_rank[r] = [(pos, w) for pos in positions]
        rank_cursor += group_size

    # Safety: if somehow we didn't fill all ranks (shouldn't happen), fill empties with zeros.
    for r in range(NCELLS):
        if not per_rank[r]:
            per_rank[r] = [(0, 1.0)]
    return per_rank


def analyze_rank_position_probs(boards: Sequence[ParsedBoard]) -> List[List[float]]:
    """Return probs[rank][pos] where rank 0=largest, pos 0..15."""
    if not boards:
        raise ValueError("No boards found in inputs (no 'board,' lines matched).")

    probs: List[List[float]] = [[0.0 for _ in range(NCELLS)] for _ in range(NCELLS)]

    for b in boards:
        per_rank = _rank_position_weights(b.cells)
        for r in range(NCELLS):
            for pos, w in per_rank[r]:
                probs[r][pos] += w

    n = float(len(boards))
    for r in range(NCELLS):
        for pos in range(NCELLS):
            probs[r][pos] /= n

    return probs


def _format_grid(values: Sequence[float]) -> str:
    # 4x4 grid, row-major.
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


def main() -> None:
    ap = argparse.ArgumentParser(description="Analyze game-over boards from 2048 training logs")
    ap.add_argument(
        "--inputs",
        nargs="+",
        required=True,
        help="Input log paths or glob patterns (e.g., logs/*.log)",
    )
    ap.add_argument(
        "--out-dir",
        default=None,
        help="If set, write rank_01.csv ... rank_16.csv under this directory",
    )
    ap.add_argument(
        "--max-boards",
        type=int,
        default=None,
        help="Optional cap for debugging (process only first N boards)",
    )
    args = ap.parse_args()

    paths = _iter_input_paths(args.inputs)
    boards = read_boards(paths)
    if args.max_boards is not None:
        boards = boards[: args.max_boards]

    probs = analyze_rank_position_probs(boards)

    print(f"boards={len(boards)}")
    for r in range(NCELLS):
        rank = r + 1
        print(f"\nrank={rank} (1=largest)")
        print(_format_grid(probs[r]))

    if args.out_dir:
        for r in range(NCELLS):
            rank = r + 1
            out_path = os.path.join(args.out_dir, f"rank_{rank:02d}.csv")
            write_csv_grid(out_path, probs[r])
        print(f"\nwrote_csv_dir={args.out_dir}")


if __name__ == "__main__":
    main()
