# 現行コードの保存版（2026-11-17）

現在の学習準備で参照するソースを、元のディレクトリ構造とバイト列のまま保存しています。既存のBeam Searchコードはこのブランチの基底コミットに含まれます。

| 対象 | 場所 |
|---|---|
| float32・A/B/Bprime/Bdoubleprime/C | `runs/fullfloat_ttas_saveable_20261116/source/` |
| 5方式共通ランチャーと実行制御 | `runs/speed_all_methods_20261117/` |
| 1T準備の設定・実行例 | `runs/nt9_1t_20261117/README.md` |
| float32モデルのgreedy評価 | `runs/fullfloat_ttas_saveable_20261116/evaluate.cpp` と `build_eval.sh` |
| float64・Dの保持実装（TTAS64） | `runs/d64_speed_20261117/source/` と `build.py` |
| archive方式の定義 | `tools/exploration_campaign/archive_conditions.json` |
| 使用可能ホストと運用制約 | `SERVER_USAGE_POLICY.json`、`AGENTS.md` |

これはソースの保存版です。学習ログ、重み、checkpoint、archive局面データ、実行バイナリ、実験出力は含めていません。参照先のデータは元ワークスペースで保持しています。各READMEにある過去の測定値は履歴の説明であり、このブランチで再測定した結果ではありません。リンク先の過去の実験結果や監査出力は未収録の場合があります。

## ビルドと実行の前提

Linux、g++（C++20）、Python 3を使います。計算・ビルドは許可されたi02〜i12の空いているホストに限定し、gwでは実行しません。Dの既存build.pyはi06のCPU 0〜3に限定されています。

新規cloneにはBUILD3.json/BUILD9.jsonとbuild生成物を含めていないため、ビルド用出力を新規作成できます。元ワークスペースの既存BUILD記録や生成物は削除・上書きしないでください。

```bash
# 空いている許可ホスト上の新規cloneで実行するビルド例
bash runs/fullfloat_ttas_saveable_20261116/build.sh 9
bash runs/fullfloat_ttas_saveable_20261116/build_eval.sh 9

# Dのビルドコマンドを表示するだけ（学習は開始しない）
python3 runs/d64_speed_20261117/build.py --digits 9
```

float32ランチャーは元環境のソースSHAとバイナリSHAを固定しています。別パス・別コンパイラでの再ビルドは元のバイナリSHAの再現を保証しないため、そのままランチャーの検査を通るとは限りません。検査を無効化したり、既存checkpointのidentityを書き換えたりしないでください。新規ビルドを使うためのidentity更新はこの保存版の対象外です。

archiveを使うB/Bprime/Bdoubleprime/Cには、別途用意した学習用局面とmanifestが必要です。元の既定manifestはこの保存版にはありません。ランチャーでは`--pool-manifest PATH`で指定します。Aはarchiveを使いません。test_launch.pyも元バイナリと既定manifestを参照するため、cloneだけでは全テストを完走できません。

Dの`plan_trial.py`は`--variant ttas64`または`futex8`で収録ソースのコマンドを表示できます。`old_d`と`precision50`は未収録の歴史releaseを必要とします。表示のみで、学習を開始しません。

今後のA/C/Dはc=0.0065固定、`--warmup-steps 0 --decay-end-steps 0`です。既存READMEの180B減衰による測定・保存結果と区別してください。Dのfull9同一バイナリ再開と各方式の1T長期品質は未検証です。

## ソースの整合性確認

各sourceディレクトリ内から、隣接するmanifestを検証できます。文字コードの異なる古いソースも変換せず保存しています。

```bash
(cd runs/fullfloat_ttas_saveable_20261116/source && sha256sum -c ../source_manifest.sha256)
(cd runs/d64_speed_20261117/source && sha256sum -c ../source_manifest.sha256)
```

未採用の`fullfloat_futex8_forward_nodup`、別診断版`d64_profile_source`、数値異常調査の`tc_ratio_followup`は現行実装として追加していません。ルートにある未コミットの解析ツールや過去の実験群も、この保存版には追加していません。
