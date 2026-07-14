## 実行方法

以下のコマンドを順番に叩くと動かし方がわかります

```sh
cd learn
make -f Makefile_VSE help
```

## 学習時スコア推移の出力

```sh
g++ analyze_score_transition.cpp -O3 -march=native -fopenmp -std=c++17 -o analyze_score_transition
./analyze_score_transition --logs-dir learn_double/logs --output score_transition.csv
python3 analyze_score_transition_plot.py --input score_transition.csv --output-dir score_transition_plots
```
