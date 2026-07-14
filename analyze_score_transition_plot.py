import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


DEFAULT_TURNS = [220, 450, 900, 1700, 3600, 7200, 14500]


def build_plot(df: pd.DataFrame, output_dir: Path, smooth_window: int) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    tuples = sorted(df["tuple"].unique())
    turns = [turn for turn in DEFAULT_TURNS if turn in set(df["turn"].unique())]

    for tuple_value in tuples:
        subset = df[df["tuple"] == tuple_value].copy()

        fig, ax = plt.subplots(figsize=(10, 6))

        for turn in turns:
            series = subset[subset["turn"] == turn].sort_values("games_processed")
            if series.empty:
                continue

            x = series["games_processed"].to_numpy()
            y = series["mean_score"].to_numpy()

            if smooth_window > 1 and len(y) >= smooth_window:
                y = pd.Series(y).rolling(smooth_window, min_periods=1).mean().to_numpy()

            ax.plot(x, y, label=f"turn={turn}")

        ax.set_title(f"Score Transition - {tuple_value}tuple")
        ax.set_xlabel("Games processed")
        ax.set_ylabel("Mean score")
        ax.grid(True, alpha=0.3)
        ax.legend()
        fig.tight_layout()

        out_path = output_dir / f"score_transition_{tuple_value}tuple.png"
        fig.savefig(out_path, dpi=300)
        plt.close(fig)
        print(f"saved {out_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot learning score transitions from score_transition.csv")
    parser.add_argument("--input", default="score_transition.csv", help="input CSV path")
    parser.add_argument("--output-dir", default="score_transition_plots", help="directory for PNG outputs")
    parser.add_argument("--smooth-window", type=int, default=1, help="optional moving-average window for smoothing")
    args = parser.parse_args()

    df = pd.read_csv(args.input)
    required = {"tuple", "turn", "games_processed", "mean_score"}
    missing = required - set(df.columns)
    if missing:
        raise SystemExit(f"missing columns: {sorted(missing)}")

    build_plot(df, Path(args.output_dir), max(1, args.smooth_window))


if __name__ == "__main__":
    main()