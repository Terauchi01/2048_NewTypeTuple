import re
import sys
import glob
import numpy as np

def top3_in_same_row_or_col(board):
    cells = []

    for r in range(4):
        for c in range(4):
            cells.append((board[r][c], r, c))

    cells.sort(reverse=True)

    r1, c1 = cells[0][1], cells[0][2]
    r2, c2 = cells[1][1], cells[1][2]
    r3, c3 = cells[2][1], cells[2][2]

    return (r1 == r2 == r3) or (c1 == c2 == c3)

files = []
for pattern in sys.argv[1:]:
    files.extend(glob.glob(pattern))

for filename in sorted(files):

    total = 0
    aligned = 0

    with open(filename) as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):

        m = re.search(r'board at -\d+ turns', lines[i])

        if m:

            board = []

            for j in range(i + 2, i + 6):
                board.append(list(map(int, lines[j].split())))

            board = np.array(board)

            if top3_in_same_row_or_col(board):
                aligned += 1

            total += 1

            i += 6
        else:
            i += 1

    ratio = aligned / total if total else 0

    print(
        f"{filename}: "
        f"{aligned}/{total} "
        f"({ratio:.4%})"
    )