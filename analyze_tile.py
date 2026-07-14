import pandas as pd
import matplotlib.pyplot as plt
import matplotlib as mpl
import os


mpl.rcParams['agg.path.chunksize'] = 10000
mpl.rcParams['path.simplify'] = True
mpl.rcParams['path.simplify_threshold'] = 1.0


csv = "placement_result_game.csv"

df = pd.read_csv(csv)


os.makedirs(
    "graphs",
    exist_ok=True
)


# 見たいターン
turns = [
    220,
    450,
    900,
    1700,
    3600,
    7200,
    14500
]


# 見たい特徴
features = [
    "straight",
    "2x2"
]


tuples = [
    6,
    7,
    8,
    9
]


for turn in turns:


    data = df[
        df["turn"] == turn
    ].copy()


    if len(data)==0:
        continue

    for seed, seed_data in data.groupby("seed"):


        seed_data = seed_data.sort_values("game")


        # 移動平均用
        window = 10000

        seed_data["game_smooth"] = seed_data["game"]

        for t in tuples:
            for feature in features:
                col = f"{t}tuple_{feature}"

                seed_data[col+"_smooth"] = (
                    seed_data[col]
                    .rolling(
                        window=window,
                        min_periods=1
                    )
                    .mean()
                )


        for feature in features:


            plt.figure(
                figsize=(10,6)
            )


            for t in tuples:


                col = f"{t}tuple_{feature}"


                plt.plot(
                    seed_data["game_smooth"],
                    seed_data[col+"_smooth"] * 100,
                    label=f"{t}-tuple"
                )


            plt.xlabel(
                "Game"
            )

            plt.ylabel(
                "Placement probability (%)"
            )


            plt.title(
                f"{feature} probability at turn {turn}, seed {seed}"
            )


            plt.ylim(
                0,
                100
            )


            plt.grid()

            plt.legend()


            plt.tight_layout()


            plt.savefig(
                f"graphs/{feature}_turn_{turn}_seed_{seed}.pdf",
                dpi=300
            )


            plt.close()

    #
    # tupleごとに全seed
    #
    for feature in features:

        for t in tuples:

            plt.figure(figsize=(10, 6))

            for seed, seed_data in data.groupby("seed"):

                seed_data = seed_data.sort_values("game")

                col = f"{t}tuple_{feature}"

                smooth = (
                    seed_data[col]
                    .rolling(window=window, min_periods=1)
                    .mean()
                )

                plt.plot(
                    seed_data["game"],
                    smooth * 100,
                    label=f"seed{seed}",
                    linewidth=0.8,
                    alpha=0.7
                )

            plt.xlabel("Game")
            plt.ylabel("Placement probability (%)")

            plt.title(
                f"{t}-tuple {feature} probability at turn {turn}"
            )

            plt.ylim(0, 100)
            plt.grid()

            plt.legend(
                fontsize=6,
                ncol=2
            )

            plt.tight_layout()

            plt.savefig(
                f"graphs/{t}tuple_{feature}_turn_{turn}_allseed.pdf",
                dpi=300
            )

            plt.close()
    
print("done")