# 15 worker 学習ランチャー（32bit、研究用）

`launch.py` は、既存の保存可能なTTAS版NT9のsourceとbinaryを読み取り、SHA-256を照合してから、方式別のコマンドを作ります。W・TCのE/Aおよびその演算はfloat32です。既定では **dry-run** で、実際に学習するには `--run` を指定します。ユーザーが指定した既存の出力先やcheckpointは上書きしません。

| 方式 | 行動 | archive |
|---|---|---|
| A | TC-UCB、paper式 | 通常開始のみ |
| B | greedy | archive開始要求50%、保存元更新なし |
| Bprime | greedy | archive開始要求50%、保存済みsourceを1回更新 |
| Bdoubleprime | greedy | archive学習step比率50%を目標、保存元更新なし |
| C | TC-UCB、source式 | archive開始要求50%、保存済みsourceを1回更新 |

全方式は15 worker、greedy-max teacher、TCの0.5 post-update、10 restart、async、compact checkpoint、cache 16 MiB/workerが共通です。B/Bprime/Bdoubleprimeの定義は `tools/exploration_campaign/archive_conditions.json` と実driverの `--archive-condition` に従います。Cのsource更新はユーザー指定により16k sourceにも適用されます。archiveが枯渇すれば通常開始にfallbackするため、要求割合やstep目標が供給割合として保証されるわけではありません。

最新のユーザー指定により、今後のAとCはTC-UCB係数 **c = 0.0065を固定**し、stepによる線形減衰を行いません。コマンドは `--ucb-c 0.0065 --warmup-steps 0 --decay-end-steps 0` です。`decay-end-steps 0` は減衰無効を意味し、係数を0にする指定ではありません。B系はgreedyのままで、探索係数を行動選択に使いません。既存の速度測定には180B/900B減衰条件が含まれ、固定係数条件の長期速度・学習品質の実証とは区別します。減衰設定もcheckpoint identityに含まれるため、過去の減衰条件のcheckpointを設定変更だけで完全再開することはできません。

例えば、i02でCのseed 8・100M stepを確認するコマンドは次のとおりです。出力親 `/tmp` は存在し、指定する出力ディレクトリ自体は存在しない必要があります。

```bash
python3 runs/speed_all_methods_20261117/launch.py --method C --seed 8 --steps 100000000 --output-dir /tmp/nt9_c_seed8
```

出力されるコマンドを確認し、実行する場合に同じ引数に `--run` を付けてください。archiveありの方式は既存の学習用manifestを既定とします。別のmanifestを使う場合は `--pool-manifest PATH` で明示します。Aはmanifestを使いません。

保存済みfull checkpointからの再開は `--resume-full CHECKPOINT_DIR` を指定できます。元と同じ方式・seed・binary・source・checkpoint schedule・cache容量などが必要で、driverはidentity不一致を拒否します。このランチャーは既存の別binary checkpointを変換・偽装しません。cache容量を `--feature-cache-mib 32` に変更するとdriver identityも変わり、16 MiB設定のfull checkpointをそのまま再開できません。対象stepは既存driverの累積step指定です。

保存は既定で最初の1M stepと、以降50B stepごと、および最終stepです。`--first-checkpoint-step` と `--checkpoint-interval` で変更できますが、driverはTC統計を50Bごとにリセットする境界でも保存します。複数runで同じ共有ストレージを使う場合は、**全runに同じ共有filesystem上の `--checkpoint-io-lock`** と、適切な `--checkpoint-io-timeout-seconds` を指定すると、対応するdriver間で保存・読み込みI/Oを順番に実行できます。既定のlockはrunごとに異なり、他runとのI/Oを制御しません。lockは異常終了後に手動点検が必要になる場合があり、別利用者のI/Oは制御できません。

`--run` 前に、予定するcheckpointのモデル本体に必要な容量と12GiBの余裕を含めて空きディスクを確認します。dry-run出力にも概算を表示します。この試算はモデル以外のログ、手動停止・失敗した保存、他runの利用分を含まず、領域を予約しません。1T・最初1M・以降50Bなら21本でモデルだけでも約395GiB/方式です。1T準備の具体例と制約は [1T実行準備](../nt9_1t_20261117/README.md) を参照してください。

実行はi02〜i12の低負荷ホストに限定し、15 CPUの利用可能性と利用可能メモリ・出力ディスクを事前確認します。実行中はメモリ・ディスク・ホスト全体のswapoutを監視し、閾値に達した場合はこのrun専用のprocess groupを停止します。他ユーザーのjobへの資源予約や、ホスト全体のswapoutの発生源特定は行えません。非公平なTTAS lockは長期待機の可能性があり、学習品質・長期進捗の同等性は未証明です。

各方式の速度・動作確認結果と制約は [方式別の結果](RESULTS.md) と [高速化実験記録](../SPEED_EXPERIMENTS_20261116.md) を参照してください。目標の平均60万steps/秒をこのランチャーが保証するわけではありません。

設定と不正引数のテスト:

```bash
python3 runs/speed_all_methods_20261117/test_launch.py
```

実行経路のsmokeはi07でC・15 worker・1,000 step指定に対し実4,045 stepで正常終了し、実driver設定と最終checkpointのmodel/driver SHAを独立確認しました。結果は `smoke_c_RESULT.json`。短いsmokeの速度は性能評価には用いません。
