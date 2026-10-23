# 7・8・9タプルの本実験

学習データは `learn_double/count004_2e11/` の以下を使用する。

- 7タプル: `tuples7-seed2-VSE-count004.dat`
- 8タプル: `tuples8-seed9-VSE-count004.dat`
- 9タプル: `tuples9-seed2-VSE-count004.dat`

学習データのseedとゲーム乱数seedは別。ゲーム乱数seedは各方式共通の0〜9。
各タプルにつきgreedy（即時報酬＋afterstate評価、深さ1）と8ビーム方式を各10ゲーム、合計270ゲーム。

```bash
make -C testplay_kmatsu build-beam-afterstate-runner-7 build-beam-afterstate-runner-8 build-beam-afterstate-runner-9
python3 scripts/run_tuple_789_experiments.py --run-dir testplay_kmatsu/results/tuple_789_new
```

まず各タプルの8ビーム方式をseed0・序盤16手で深さ測定し、1手平均1秒を初めて超える直前の深さを選ぶ。
上限512、幅上限2048、Fixed=64、SqrtBudgetの幅計算用budget=100000。
SqrtBudgetは幅計算にbudgetを使用し、評価回数予算による打ち切りはしない。
深さ測定は8ワーカー、完走ゲームは16ワーカーで、タプルは7→8→9の順に実行する。

保存物:

- `parameters.json`: 学習データseed、ゲーム数、設定。
- `phase_times.csv`: 各タプルの深さ測定・ゲーム実行それぞれの開始終了時刻、実時間、終了コード。
- `summary.csv`: 完了したタプルを順次集計。
- `tuple7_seed2/`、`tuple8_seed9/`、`tuple9_seed2/`:
  - `calibration/`: 各深さの測定値、選択した深さ、実到達深さ。
  - `game_scores.csv`: 1ゲーム終了ごとに追記・flush。スコア、探索時間、実時間、開始終了時刻（Unixマイクロ秒）、タプル種別。
  - `games/`: 個別ゲームの詳細。
  - `summary.csv`: 方式ごとの平均スコア、探索時間合計、ゲーム実時間合計、最初のゲーム開始〜最後のゲーム終了までの実時間。
  - `timing.json`: タプル全体の実時間（深さ測定とデータ読み込みを含む）。
  - `STATUS`、`*.PID`、`calibration.log`、`games.log`。

`total_game_wall_seconds`は並列ゲームの実時間を加算した値であり、方式の経過時間は`method_elapsed_seconds`を使用する。
データ読み込みと深さ測定を含む全体の経過時間は`timing.json`を使用する。

評価器はTUPLE_FILE_TYPEで6/7/8/9を切り替え、既存double expectimaxと同じフィルター、stage/split/tuple/indexの保存順を使用する。
`build-evaluator-audit-7/8/9`で各監査バイナリを作成できる。実データの全バイト読み込み一致、盤面評価、深さ3/4の枝刈りなしexpectimax一致を検査する。
