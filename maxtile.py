import re
import sys
import glob
import numpy as np
from collections import defaultdict

if len(sys.argv) < 2:
    print(f"Usage: python {sys.argv[0]} <files>")
    sys.exit(1)

# 角
corner_positions = [
    (0, 0), (0, 3),
    (3, 0), (3, 3)
]

# 角の隣
edge_positions = [
    (0, 1), (0, 2),
    (1, 0), (1, 3),
    (2, 0), (2, 3),
    (3, 1), (3, 2)
]

# ワイルドカード展開
files = []
for pattern in sys.argv[1:]:
    files.extend(glob.glob(pattern))

for filename in sorted(files):

    print(f"\n==============================")
    print(filename)
    print(f"==============================")

    counts = defaultdict(lambda: np.zeros((4, 4), dtype=int))

    with open(filename, "r") as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):

        line = lines[i]

        m = re.search(r'board at (-\d+) turns', line)

        if m:
            turn = int(m.group(1))

            board = []

            for j in range(i + 2, i + 6):
                row = list(map(int, lines[j].split()))
                board.append(row)

            board = np.array(board)

            max_value = np.max(board)

            positions = np.argwhere(board == max_value)

            for r, c in positions:
                counts[turn][r][c] += 1

            i += 6
        else:
            i += 1

    for turn in sorted(counts.keys()):

        print(f"\n=== board at {turn} turns ===")

        matrix = counts[turn]

        for row in matrix:
            print(*row)

        total = np.sum(matrix)

        if total > 0:

            # 角
            corner_sum = sum(matrix[r][c] for r, c in corner_positions)
            corner_ratio = corner_sum / 4

            # 角の隣
            edge_sum = sum(matrix[r][c] for r, c in edge_positions)
            edge_ratio = edge_sum / 8

            print()
            print(f"corner avg: {corner_ratio:.3f}")
            print(f"edge avg:   {edge_ratio:.3f}")

            print(f"corner total: {corner_sum}/{total}")
            print(f"edge total:   {edge_sum}/{total}")