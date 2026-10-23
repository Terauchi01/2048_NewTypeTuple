# ビームサーチ高速化・比較（2026-10-21）

同一PC（AMD Ryzen 9 3950X）、同一盤面・同一dat・同一評価関数で単一探索を比較。-O3、ウォームアップ1回後5回の中央値。dat読み込み時間は除外。固定幅64。

| 盤面 | 深さ | 高速化前 ms | 高速化後 ms | 速度比 |
|---|---:|---:|---:|---:|
| 序盤2タイル | 4 | 25.610 | 5.938 | 4.31倍 |
| 序盤2タイル | 8 | 74.559 | 16.924 | 4.41倍 |
| 序盤2タイル | 12 | 111.789 | 25.454 | 4.39倍 |
| 中盤 | 4 | 6.468 | 2.547 | 2.54倍 |
| 中盤 | 8 | 17.967 | 6.023 | 2.98倍 |
| 中盤 | 12 | 31.298 | 10.461 | 2.99倍 |
| 終盤寄り | 4 | 1.821 | 0.843 | 2.16倍 |
| 終盤寄り | 8 | 6.928 | 2.651 | 2.61倍 |
| 終盤寄り | 12 | 14.611 | 4.973 | 2.94倍 |

変更: 1探索内の同一盤面評価をキャッシュ（盤面を完全一致照合、探索ごとに世代を更新）、chance節の子を固定長配列化、親のchance節を連続範囲で管理、4方向のメモリ容量再利用、全候補sortをnth_element＋採用候補sortに変更。対称形や確率の異なる探索パス自体は統合せず、従来の探索結果を保持する。

検証: 全8方式・15盤面・深さ1/3/4/8・3評価関数の計1,440条件で、高速化前と全方向の値・選択手・生成数・枝刈り数・保持数が一致。評価関数の差し替え、NaN拒否、SqrtBudgetの平方根境界値と上限も検査。AddressSanitizer/UndefinedBehaviorSanitizerを使用（実行環境のptrace制約によりLeakSanitizerのみ無効）。実datでは既存double expectimaxとの葉評価一致、枝刈りなし深さ3/4の値一致も確認。

## コピー元と実装差

- `miacp_2048_CG2026/`: `/home2/matsu-lab2/kmatsu/2048_CG2026`。`play_expectimax.cpp` は固定幅。コピー元と主要4ファイルのSHA-256一致を確認。元のソースは編集せずadapterだけ追加。
- `miacp_2048_tree_search_CG2026/`: `/home2/matsu-lab2/kmatsu/2048_tree_search_CG2026/src/cpp`。git HEAD `9459fb108b6aefbfbe7359b70a03e133052e2f7a`。`BEAM_WIDTH=0` が `floor(sqrt(budget))`。
- コピー元は全root共通のfrontier、対称盤面の再利用、事前確保arena、評価回数予算による早期終了を使う。現在の方式はroot方向別frontierであり、コピー元と同じ幅の数字でも同じ探索量ではない。
- コピー元の評価関数は40参照・17進添字・学習状態を含むdat。現在は72参照・18進添字・評価値のみのdat。元datを直接取り替えて比較できないため、benchmark_adapter.cppで同じ現在の評価関数とmoveBを使って探索部分を比較した。
- 固定幅60比較の原始データは `beam_width60.csv`、コピー元全体幅240にしたものは `beam_width240.csv`。同じ総幅に近づけると密な盤面では現在の旧実装の方が速い条件もあり、単なる実装差だけで一律の倍率とは言えない。

## 8方式と1秒の測定

`fixed, sqrt, sqrt_budget, cbrt, power, increasing, decreasing, adaptive`。既存 `sqrt` は深さdに対する `2^d`。追加 `sqrt_budget` は `floor(sqrt(BUDGET))` を各rootの幅に使い、既定budget=100000で幅316。どちらもmax_widthで上限をかける。今回追加したbudgetは幅計算の入力であり、コピー元の評価回数での早期終了は導入していない。

`bash testplay_kmatsu/run_afterstate_speed_calibration.sh` はseed0・序盤16手・方式ごとに深さ1から測り、平均1秒を初めて超える直前の深さを選ぶ。8方式を並列で測定し、1探索自体は単一スレッド。既定上限512。残す枝が尽きてそれ以上深く探索できない場合は `frontier_exhausted`、上限まで1秒未満なら `depth_limit` と記録する。要求深さと実際に到達した深さの範囲もCSVに保存する。序盤16手の平均なので、全局面の1秒以内を保証するものではない。

再現: `make -C testplay_kmatsu test-beam-afterstate-optimization`。実dat照合: `make -C testplay_kmatsu build-evaluator-audit` 後、`./testplay_kmatsu/audit_evaluator_6 audit learn/dat/tuples6-seed0-VSE-count004.dat /tmp/optimized_audit`。
