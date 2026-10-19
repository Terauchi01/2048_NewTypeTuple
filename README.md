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

## 6-tuple Beam Search 実験

Beam Search の単体テストと6-tuple用実験バイナリは次のコマンドで作成できます。

```sh
make -C testplay_kmatsu test-beam
make -C testplay_kmatsu beam
./testplay_kmatsu/test_beam_search_6 \
  0 10 4 sqrt 2048 tuples6-seed0-VSE-count004.dat \
  --threads 1 --max-moves 0 --timeout-sec 60
```

必須引数は順に `seed game_count depth method max_width evfile` です。`depth` はroot手を含む
プレイヤー手数（`1` はgreedy）、Beam層の深さ `d` はroot後の最初の展開を1と数えます。
全方式で幅は1以上かつ `max_width` 以下です。`method` と層ごとの上限は次の7種類です。

- `fixed`: `k`（`--k`）
- `sqrt`: `2^d`
- `cbrt`: `ceil(4^(d/3))`
- `power`: `ceil(4^(alpha*d))`（`--alpha`）
- `increasing`: `ceil(W0*r^(d-1))`（`--w0`, `--ratio`）
- `decreasing`: `ceil(W0*r^(-(d-1)))`
- `adaptive`: 候補値の母標準偏差を `stddev` として
  `value >= best - sigma*stddev`（`--sigma`）を満たす候補数

上限は各層の候補を評価し終えてからfrontierへ適用するため、その層自身ではなく次層以降の
展開量を抑えます。同値は生成順で決定的に処理します。`--threads`（既定5）、校正用の
`--max-moves`（0は終局まで）、1手の `--timeout-sec`（1〜60、既定60）も指定できます。
timeout時はそのゲームを直ちに打ち切り、runnerは終了コード124を返します。

評価値には `learn_double` が出力する学習済み6-tuple doubleファイル（`count004`など）が
必要です。raw形式と `.xz` の両方を読み込めます（`.xz` には `xz` コマンドが必要）。
期待サイズは612,220,032個のdouble、4,897,760,256バイトです。int/Q10版、タプル型違い、
切れたファイル、余分なデータ、壊れたxzストリームはエラーになります。double評価値と
ゲーム報酬は同じ尺度なので `reward_scale` は1です。

各ゲームの出力は従来と同じ `game,sco,big,turn` から始まり、実move数とstatus、方式と全parameter、
探索統計 `expanded,generated,evaluated,pruned`、実効幅の
`beam_layers,effective_width_min,effective_width_max,effective_width_avg,retained`、
`search_time_us` と `avg_search_us` が続きます。複数threadでは行順は一定ではありませんが、
seedごとの結果は再現可能です。ゲームはatomic queueで配るため端数も欠落しません。

### 7方式の自動比較

次のlauncherは処理を `screen`（利用できない環境では `nohup`）でdetachして即座に戻ります。
チャットやterminalを閉じても実験は継続します。

```sh
./testplay_kmatsu/launch_beam_comparison.sh \
  tuples6-seed0-VSE-count004.dat
```

既定の保存先は
`testplay_kmatsu/results/beam_comparison_<timestamp>/` です。launcherはPID、STATUSファイル、
絶対保存先を表示します。各方式を1 game・少数手・1 threadでdepth sweepし、
`search_time_us / moves` が平均1秒に最も近いdepthを選びます。1手60秒に達したdepthは除外して
その方式のsweepを打ち切り、選択depthでゲームseed 0〜9を順次実行します。

保存先には `run.log`, `calibration.csv`, `calibration/*.log`, seedごとの `games/*.csv`,
`summary.csv`, `PID`, `STATUS` が作られます。同じディレクトリを指定すると完了済みの方式、校正、
seedをskipして再開できます。

進捗は `cat <保存先>/STATUS` と `tail -f <保存先>/run.log` で確認できます。

```sh
RUN_DIR=/absolute/path/to/previous/result \
  ./testplay_kmatsu/launch_beam_comparison.sh tuples6-seed0-VSE-count004.dat
```

`MAX_WIDTH`, `FIXED_K`, `ALPHA`, `W0`, `RATIO`, `SIGMA`, `DEPTH_MIN`, `DEPTH_MAX`,
`CALIBRATION_MOVES`, `TARGET_MS` を環境変数で上書きできます。比較scriptの既定値は
`MAX_WIDTH=2048`, `FIXED_K=64`, `ALPHA=0.4`, `W0=64`, `RATIO=2`, `SIGMA=1`,
`TARGET_MS=1000` です。
