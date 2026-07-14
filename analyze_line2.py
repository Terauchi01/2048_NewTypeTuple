import sys
import glob

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

    scores = []

    total_boards = 0
    aligned_boards = 0

    with open(filename) as f:
        for line in f:

            if line.startswith("game,"):

                parts = line.strip().split(",")

                try:
                    score = int(parts[3])
                    scores.append(score)
                except:
                    continue

            elif line.startswith("board,"):

                parts = line.strip().split(",")

                try:
                    vals = list(map(int, parts[1:]))
                except ValueError:
                    continue

                if len(vals) != 16:
                    continue

                board = [
                    vals[0:4],
                    vals[4:8],
                    vals[8:12],
                    vals[12:16]
                ]

                total_boards += 1

                if top3_in_same_row_or_col(board):
                    aligned_boards += 1

    mean_score = sum(scores) / len(scores) if scores else 0
    ratio = aligned_boards / total_boards if total_boards else 0

    print(
        f"{filename:<40} "
        f"mean={mean_score:10.1f} "
        f"{aligned_boards}/{total_boards} "
        f"{ratio:.4%}"
    )