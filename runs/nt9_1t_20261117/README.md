# 5方式・1T step の実行準備

対象は A、B、Bprime（B′）、Bdoubleprime（B″）、C。それぞれ15 worker、全float32、1,000,000,000,000 stepを目標とし、50,000,000,000 stepごとにcheckpointを保存する。最新のユーザー指定に従い、今後のA/CはTC-UCB係数c=0.0065を固定し、線形減衰を無効にする（`--decay-end-steps 0`。係数を0にする意味ではない）。B系はgreedyのまま。各方式の行動・archive条件は[共通ランチャー](../speed_all_methods_20261117/README.md)を参照。

2026-11-17追記：下記の指数17同士の合体は、任意の人工入力では再現するが、[Rodgers and Levine](https://phil.rodgers.ai/papers/2048_CIG2014.pdf) による標準4×4・2/4出現ルールの理論上限は指数17であり、通常の合法プレイからこの合体に到達するとは言えない。元の注意書きは通常ルールでの実運用上の危険を過大評価していた。ただし本driverはepisode内restartで過去のafterstateを復元した直後、新しいタイルをspawnせず次の手を打つため、論文の前提と異なる。外部archive局面も独立した全履歴の再生による到達可能性は未確認。本実装での指数18の到達可能性は未解決であり、高指数対応の拡張を採用する判断は保留する。

**準備中。これらのコマンドは既定でdry-runであり、学習を開始しない。** 指数18への拡張はユーザーの判断により今回の対象外。全方式で平均60万steps/秒、A/C固定係数設定での長期品質、50B/1Tの実境界、archiveが十分供給される運用はまだ確認できていない。過去の180B/900B減衰条件の測定は履歴として区別する。特に短期試験ではarchive donorが枯渇し、要求50%を実現できなかった。[方式別の結果](../speed_all_methods_20261117/RESULTS.md)

例えば各コマンドをi02〜i12の空いている別マシンで実行する。以下はプロジェクトのルートから実行するdry-runで、出力先は共有ワークスペース上の別ディレクトリ、I/O lockは5方式共通である。`--run` は含まれない。

```bash
for method in A B Bprime Bdoubleprime C; do
  python3 runs/speed_all_methods_20261117/launch.py \
    --method "$method" --seed 0 --steps 1000000000000 \
    --output-dir "runs/nt9_1t_20261117/$method" \
    --first-checkpoint-step 1000000 --checkpoint-interval 50000000000 \
    --feature-cache-mib 32 \
    --checkpoint-io-lock "runs/nt9_1t_20261117/checkpoint_io.lock" \
    --checkpoint-io-timeout-seconds 43200
done
```

開始する際は各方式を別々の許可ホストで実行し、他の利用者のCPU・RAM・ディスク・I/Oを確認したうえで `--run` を明示する。実行時のseed・方式・保存間隔・cache容量・binary/sourceはfull checkpoint identityに含まれるため、再開時は一致させる。旧方式との互換性は要求されないが、新方式の同一binary再開と完全なcheckpointの検証は必要。

最初の1Mに加え50Bごとの20本を削除せずに保持するため、既存full9 checkpointの実測値では**モデルだけで約395GiB/方式、5方式で約1.93TiB**。ログ、driver metadata、途中失敗時の一時保存、手動保存、他job分は別途必要。実行前の容量確認は予約ではなく、同じfilesystemで複数runを始める際は合計容量とquotaを別途確認する。調査時の共有NFS空きは約17.77TiBだったが、共有環境で容量・性能・安定性を保証しない。許可hostのローカル `/tmp` は調査時点ですべて21本を保持する空きに不足していた。[容量調査](../1t_storage_capacity_20261117.md)

同一の共有lockを指定すれば、このdriver群のcheckpoint保存・読込を直列化できる。lock pathは全ホストから同じ実体として見える共有filesystem上に置く。待ち時間がtimeoutを超えると停止し、他利用者のI/Oは制御できない。ホスト全体のswapoutや空き容量のguardが発動して未完了checkpointが残った例があるため、各保存の `COMPLETE`・`checkpoint_status.csv`・SHAを確認する。

cache32MiB/workerは1B時点の調査で履歴超過を減らしたが、A/C固定係数を含む全5方式の1T性能は未検証。初期1M checkpointなしで50B単位の20本だけにする場合は `--first-checkpoint-step 50000000000` とする。ただし最初の50Bまで進捗を保存できない。各waveの`actual_steps`はepisode終了まで進むため、要求stepより上回る場合がある。
