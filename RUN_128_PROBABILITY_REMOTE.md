# 172.21.52.93で128タイル作成確率を解析するためのプロンプト

以下を、転送先マシンを操作するAIへそのまま渡してください。

```text
作業ディレクトリ `~/terauchi/2048_NewTypeTuple` に転送済みのコードを使い、
上位8タイル固定時に残り8マスから非保護の128タイルを新規作成できる理論確率を、
7種類の配置について計算してください。

解析定義:
- 保護タイルは16384,8192,4096,2048,1024,512,256,128の8枚。
- 既存の保護128は成功判定に含めない。非保護128を新たに作れば成功。
- 保護8タイルの移動、値変更、結合は禁止。移動予算は0。
- 標準2048のスワイプを盤面全体へ適用し、保護条件を満たす手だけを合法とする。
- ゲーム開始前に自由8セルへ異なる位置で2回spawnする。
- 各手後のspawn位置は、move後の空きセルから一様に選ばれる。
- spawn値は2が90%、4が10%。
- プレイヤーは非保護128到達確率を最大にする手を各状態で選ぶ。
- 求めるのは最適方策下の到達確率。状態数に対する成功状態の比率や
  Monte Carlo試行率ではない。

この比較の目的は、直線配置と2×2配置で2枚目の32768作成成功率が異なる理由の
一つとして、残り8マスにおける下位タイル再構築能力の差を示すことです。

実装は、moveが自由タイル総和を保存し、spawnが総和を必ず2または4増やすため、
状態を総和の降順に処理する非循環動的計画法になっている。固定小数点の上下界も
同時に伝播し、真の確率を含む認証区間を出力する。

次の順に実行してください。勝手に解析規則やソースを変更しないでください。

1. ビルドとセルフテスト:

   make -f Makefile.exact_analysis clean
   make -f Makefile.exact_analysis
   ./exact_second_32768 --self-test

2. 代表直線型のsmoke test:

   ./exact_second_32768 \
     --rank-cells 0,1,2,3,7,6,5,4 \
     --protected-max-exponent 14 \
     --target-exponent 7 \
     --mode probability \
     --max-states 10000000 \
     --max-seconds 600 \
     --progress-interval 0

   `protected_values=16384,8192,4096,2048,1024,512,256,128`、
   `target_tile=128`、`complete=true`、`possible=true`、および
   `optimal_success_probability_lower/upper`の存在を確認する。参考値は約79.466%。

3. 7配置をバックグラウンド実行:

   JOBS=4 MAX_STATES=10000000 MAX_SECONDS=604800 \
     ./start_128_probability_background.sh

   マシンのメモリが十分ならJOBS=7へ増やしてよい。不明なら4のままにする。

4. 状態確認:

   ./check_128_probability_job.sh
   tail -n 50 results_probability_128/launcher.log

5. 完了後の集計:

   ./summarize_128_probabilities.sh | tee results_probability_128/summary.tsv

完了条件:
- 7/7配置がfinishedになる。
- 全result.txtがcomplete=trueである。
- 全結果のprotected_max_exponentが14、target_exponentが7である。
- 各結果にoptimal_success_probability_lower/upperが存在する。
- lower <= probability <= upperで、interval_widthを報告する。
- 配置ごとのpercent、states、wall_seconds、peak_rss_kbを表にして返す。
- 直線型代表と2×2型代表の成功率の差および比率を計算する。
- エラー時は再実行やソース変更をせず、launcher.logと該当progress.txtの末尾を返す。
```

## 人間が直接実行する場合の最短手順

```bash
cd ~/terauchi/2048_NewTypeTuple
make -f Makefile.exact_analysis
./exact_second_32768 --self-test
JOBS=4 ./start_128_probability_background.sh
./check_128_probability_job.sh
```
