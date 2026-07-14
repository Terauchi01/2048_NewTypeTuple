import argparse
import csv
import os
from collections import defaultdict

import matplotlib.pyplot as plt


DEFAULT_TURNS = [220, 450, 900, 1700, 3600, 7200, 14500]
DEFAULT_FEATURES = ["straight", "block2x2"]
DEFAULT_TUPLES = [6, 7, 8, 9]


def moving_average(values, window):
    if window <= 1:
        return values

    smoothed = []
    total = 0.0
    left = 0

    for right, value in enumerate(values):
        total += value

        while right - left + 1 > window:
            total -= values[left]
            left += 1

        smoothed.append(total / (right - left + 1))

    return smoothed


def main():

    parser = argparse.ArgumentParser()
    parser.add_argument(
        "csv",
        nargs="?",
        default="placement_result_turn_progress.csv"
    )
    parser.add_argument(
        "--outdir",
        default="graphs"
    )
    parser.add_argument(
        "--window",
        type=int,
        default=10000
    )
    parser.add_argument(
        "--turns",
        nargs="*",
        type=int,
        default=DEFAULT_TURNS
    )

    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    with open(args.csv, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    if not rows:
        print("no data")
        return

    # turnごと
    by_turn = defaultdict(list)
    for row in rows:
        by_turn[int(row["turn"])].append(row)

    for turn in args.turns:

        turn_rows = by_turn.get(turn, [])

        if not turn_rows:
            continue

        # featureごと
        for feature in DEFAULT_FEATURES:

            # tupleごとに1枚
            for tuple_id in DEFAULT_TUPLES:

                plt.figure(figsize=(10, 6))

                # seedごとに線を描く
                by_seed = defaultdict(list)

                for row in turn_rows:
                    if int(row["tuple"]) == tuple_id:
                        by_seed[int(row["seed"])].append(row)

                for seed in sorted(by_seed.keys()):

                    seed_rows = by_seed[seed]

                    seed_rows.sort(
                        key=lambda r: int(r["game"])
                    )

                    games = [
                        int(r["game"])
                        for r in seed_rows
                    ]

                    values = [
                        float(r[feature]) * 100.0
                        for r in seed_rows
                    ]

                    values = moving_average(
                        values,
                        args.window
                    )

                    plt.plot(
                        games,
                        values,
                        label=f"seed{seed}"
                    )

                plt.xlabel("Game")
                plt.ylabel("Placement probability (%)")

                plt.title(
                    f"{tuple_id}-tuple {feature} probability (turn={turn})"
                )

                plt.ylim(0, 100)
                plt.grid(True)
                plt.legend()

                plt.tight_layout()

                out = os.path.join(
                    args.outdir,
                    f"{tuple_id}tuple_{feature}_turn_{turn}.pdf"
                )

                plt.savefig(
                    out,
                    dpi=300
                )

                plt.close()

    print("done")


if __name__ == "__main__":
    main()