import pandas as pd
import matplotlib.pyplot as plt

WINDOW = 10000

df = pd.read_csv("first_end_scores.csv")

for t in sorted(df["tuple"].unique()):

    plt.figure(figsize=(8, 5))

    seeds = sorted(df[df["tuple"] == t]["seed"].unique())

    print(f"\n===== {t}-tuple =====")

    for seed in seeds:

        d = (
            df[
                (df.tuple == t) &
                (df.seed == seed)
            ]
            .sort_values("steps")
            .copy()
        )

        if len(d) == 0:
            continue

        # 各seedの最終stepを表示
        print(
            f"seed {seed}: "
            f"games={len(d):8d}, "
            f"last_step={d['steps'].iloc[-1]:15,d}"
        )

        d["score_ma"] = (
            d["score"]
            .rolling(window=WINDOW, min_periods=1)
            .mean()
        )

        plt.plot(
            d["steps"],
            d["score_ma"],
            label=f"seed{seed}"
        )

    plt.title(f"{t}-tuple")
    plt.xlabel("Learning Steps")
    plt.ylabel(f"Moving Average Score ({WINDOW} games)")
    plt.xlim(0, 200_000_000_000)
    plt.ylim(0, 500_000)
    plt.grid(True)
    plt.legend()

    plt.tight_layout()
    plt.savefig(f"{t}tuple_score_transition_steps_ma{WINDOW}.pdf")
    plt.close()

print("done")