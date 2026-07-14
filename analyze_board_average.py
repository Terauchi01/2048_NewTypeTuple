import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import os

os.makedirs("graphs/heatmap", exist_ok=True)
os.makedirs("graphs/heatmap/seed", exist_ok=True)

df = pd.read_csv("board_average_log2_top3.csv")

turns = [
    220,
    450,
    900,
    1700,
    3600,
    7200,
    14500
]

tuples = [6, 7, 8, 9]

# ====================================================
# 全seed平均ヒートマップ
# ====================================================

for turn in turns:

    for t in tuples:

        row = df[
            (df.turn == turn) &
            (df.tuple == t)
        ]

        if len(row) == 0:
            continue

        row = row.iloc[0]

        log_board = np.zeros((4, 4))

        for idx in range(16):
            log_board[idx // 4, idx % 4] = row[f"c{idx}"]

        plt.figure(figsize=(5, 5))

        plt.imshow(
            log_board,
            cmap="viridis"
        )

        for r in range(4):
            for c in range(4):
                plt.text(
                    c,
                    r,
                    f"{log_board[r,c]:.2f}",
                    ha="center",
                    va="center",
                    fontsize=12
                )

        plt.title(
            f"{t}-tuple turn={turn} average"
        )

        plt.colorbar(
            label="average log2(tile)"
        )

        plt.xticks([])
        plt.yticks([])

        plt.tight_layout()

        plt.savefig(
            f"graphs/heatmap/logcolor_average_board_{t}tuple_turn{turn}.pdf",
            dpi=300
        )

        plt.close()


# ====================================================
# seed別ヒートマップ
# ====================================================

if "seed" in df.columns:

    seeds = sorted(df["seed"].unique())

    for seed in seeds:

        for turn in turns:

            for t in tuples:

                row = df[
                    (df.seed == seed) &
                    (df.turn == turn) &
                    (df.tuple == t)
                ]

                if len(row) == 0:
                    continue

                row = row.iloc[0]

                log_board = np.zeros((4, 4))

                for idx in range(16):
                    log_board[idx // 4, idx % 4] = row[f"c{idx}"]

                plt.figure(figsize=(5, 5))

                plt.imshow(
                    log_board,
                    cmap="viridis"
                )

                for r in range(4):
                    for c in range(4):
                        plt.text(
                            c,
                            r,
                            f"{log_board[r,c]:.2f}",
                            ha="center",
                            va="center",
                            fontsize=12
                        )

                plt.title(
                    f"{t}-tuple turn={turn} seed={seed}"
                )

                plt.colorbar(
                    label="average log2(tile)"
                )

                plt.xticks([])
                plt.yticks([])

                plt.tight_layout()

                plt.savefig(
                    f"graphs/heatmap/seed/logcolor_average_board_{t}tuple_turn{turn}_seed{seed}.pdf",
                    dpi=300
                )

                plt.close()

print("done")