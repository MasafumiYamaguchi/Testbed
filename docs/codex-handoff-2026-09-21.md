# Codex 引き継ぎ — 2026-09-21 JST

ユーザーの「Codexに引き渡すのでちょうどいいところで止めて」に従い、
新規開発を停止し、検証済みの Freeze 基盤と未完成の並行作業を保存した。
当初の 08:00 JST までの継続指示より、この停止指示を優先する。

## 継続する前提

- リポジトリ: `MasafumiYamaguchi/Testbed`。Issue #12 は対象外。
- 複数エージェント利用はユーザーが明示的に許可済み。
- `main` へはマージしていない。積み重ねた PR を独立にマージしない。
- 2026-09-20 改訂の用途は「成長・風 → 選択状態 → Freeze → 仕上げ → 単一 VDB」。
  時系列シミュレーションや VDB シーケンスは対象ではない。
- 物理 RTX での検証はユーザー指示で後回し。次の実装フェーズへ進むことは許可済み。
  CI 成功を見た目の自然さ、RTX 性能、Issue の全受入条件の完了と扱わない。
- 今回は Issue を閉じていない。未完成項目を下記のとおり残している。

## PR の積み重ね

| PR | ブランチ | 内容・確認状況 |
|---|---|---|
| [#74](https://github.com/MasafumiYamaguchi/Testbed/pull/74) | `codex/phase2-cumulonimbus` | 積乱雲 prefab。Windows 実入力と画像確認済み |
| [#75](https://github.com/MasafumiYamaguchi/Testbed/pull/75) | `codex/phase2-centerline` | 中心線・高度 profile。後続 PR の CI でも回帰検証 |
| [#76](https://github.com/MasafumiYamaguchi/Testbed/pull/76) | `codex/phase2-developed-cells` | 独立した発達セル。現 head の全 CI 成功、画像確認済み |
| [#77](https://github.com/MasafumiYamaguchi/Testbed/pull/77) | `codex/phase2-growth-wind` | 成長段階・高度別風・雲頂階層。Release 後半の回帰検証中 |
| [#78](https://github.com/MasafumiYamaguchi/Testbed/pull/78) | `codex/phase2-anvil-integration` | かなとこ雲・風との接続・width/direction handle。Release 検証中 |
| [Freeze branch](https://github.com/MasafumiYamaguchi/Testbed/tree/codex/phase2-frozen-state) | `codex/phase2-frozen-state` | #78 が base の draft PR。固定結果・独立 detail・schema 10 保存。CPU 36/36 成功、Windows 検証待ち |

Freeze は Issues #33/#38 の基盤であり、全仕上げ機能の完了ではない。
`docs/adr/0029-selected-growth-and-wind.md`、`0030-anvil-field-and-wind.md`、
`0031-frozen-cloud-state-and-persistence.md` がモデルと上限を説明する。

## Freeze の確定した区切り

- 評価済みの double 精度 Recipe、curve、hierarchy、anvil、ID、noise origin を保存し、
  描画・detail 編集・再読込で構造生成を呼ばない。
- `content_hash` は固定構造、`payload_hash` は保存内容の破損検出、
  `frozen_density_hash` は有効な密度入力の識別。用途を混同しない。
- schema 10 は一つの `cloud.kind=frozen` を保存。旧 schema 1–9 を移行し、
  未知の生成アルゴリズム版でも既存の固定結果を読める。再生成だけ無効になる。
- 非同期 Generate → Freeze/Adopt、Cancel/Retry、Undo/Redo、Save/Open の
  native smoke test と選択状態/Frozen の HDR 比較を追加した。
- 平行移動後の高度 profile、Frozen anvil の float 誤差予算、
  cut と object/field/primitive の ID 衝突をレビューして修正した。
- 上記を含む最終ローカルコード `0ef51a8` は Release CPU 36/36 成功。
  保存時に GitHub API で commit を再構成するため remote SHA は異なる。
  アプリ側 C++ syntax check は通過。新 Freeze の Windows/HLSL/実入力結果は未確認。

## 再開時の最初の作業

1. Freeze PR の Actions を確認する。Windows Release の
   `Frozen selected field and lifecycle comparisons` と早期 artifact
   `windows-Release-selected-state-evidence` を優先する。
2. `scripts/capture-freeze.ps1` の Generate/Freeze/finish/cancel/retry/replace markers、
   calm/wind × front/side の selected/Frozen HDR 比較、別プロセス再読込の
   `frozen_reload_generation_jobs=0` を確認し、実際の PNG を開く。
3. #77/#78 の最終 Release artifact を取得して画像・JSON・数値ログをレビューする。
   成功した step と未確認の画像を分けて PR 本文を更新する。
4. 仕上げレイヤーとプリセットの WIP は下記ブランチから必要な差分だけ統合する。
   WIP の共通 base は Freeze の初期基盤であり、最新 Freeze の数値修正と
   native CLI/workflow は含まれない。WIP の tree で最新ブランチ全体を置換しない。

## 永続保存した並行作業の checkpoint

両ブランチの親 `9f7c06400394188d73fc0d6d0e9e49c97b7193c2` は
ローカル `bc80e12` と同じ tree の Freeze 初期基盤。
各 WIP はその親から一つの commit として保存した。PR は開いていない。

| Issue | Remote branch / head | 検証済み範囲 | 主な残作業 |
|---|---|---|---|
| #34 | `codex/wip-issue34-modifier-stack` / `add134ec453b3a8f803c74194ccb53848d097725` | CPU 37/37、UI syntax。最大 4 の順序付き cut / density / detail protection、schema 11、UI hooks | 最新 Freeze 修正との統合、modifier 倍率を含む GPU 誤差予算、CLI/capture 接続、DXC/Windows 実測 |
| #37 | `codex/wip-issue37-presets` / `84bb304f780a44e903756f4224e900d595472dfb` | Release core build、専用 preset test 1/1。4 preset、seed 範囲、共有 view helper | preset/比較 UI、実 GPU preview、ID 再発行、破棄確認、finish layer との統合 |

詳細は各保存 commit の
[#34 checkpoint](https://github.com/MasafumiYamaguchi/Testbed/blob/add134ec453b3a8f803c74194ccb53848d097725/docs/issue-34-checkpoint.md) と
[#37 checkpoint](https://github.com/MasafumiYamaguchi/Testbed/blob/84bb304f780a44e903756f4224e900d595472dfb/docs/issue-37-checkpoint.md)。
どちらも Windows 合格・Issue 完了とは扱わない。初めに #34 を最新 Freeze へ統合し、
次に #37 の UI を接続すると adoption guard の競合を整理しやすい。

ローカルの対応は以下。保存済み remote の commit identity とは異なる。

- `/workspace/scratch/59c2b798b94d/modifiers`:
  `codex/phase3-modifier-integration`、`aefcfbc5d4defc0bc55861fb02ca08dd97018a66`。
  branch 名の phase3 は作業用名称で、Issue #34 は Phase 2 の継続。
- `/workspace/scratch/59c2b798b94d/presets`:
  `codex/phase2-presets`、`880ecdfa23d9721890bd845095b516f4fcd93c82`。

## CI と保存済み証拠

- #76 head: `864e0e5c6e540bce3dc0d39ba9e67f0cb77e8a4e`。
  [Native 35530527905](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35530527905)
  および [reference 35530527906](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35530527906) 成功。
  検証済みの PNG/EXR/JSON/ログと SHA manifest は
  `docs/evidence/phase2-selection/developed-pr76/REVIEW.md` を参照。
- #77 head: `1b1566654fea282c838aae89944d728ea8f1ed61`。
  [Native 35531174858](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35531174858)、
  [reference 35531174856](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35531174856)。
  reference、CPU Debug/Release、Windows Debug は成功。
  19:59:54 UTC 時点で Release の growth/top/developed/prefab/centerline/Phase-0/HG/sun 比較は成功、
  conservative empty-space 比較中。Release artifact は未公開、growth/top 画像は未取得。
  `docs/evidence/phase2-selection/CHECKPOINT.md` に job/artifact IDs と取得手順、
  `acceptance-status.md` に受入条件対応表を残した。
- #78 head: `ff6ed399700f8925a0929bdec3f0a6f9291f1881`。
  [Native 35532876269](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35532876269)、
  [reference 35532876274](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35532876274)。
  19:58 UTC 時点で reference、CPU Debug/Release、Windows Debug は成功。
  Release の build/CPU/actual app/growth は成功し、Anvil 比較が実行中。
  その実画像を確認したとはまだ報告していない。

CI は停止せず結果を残す。これは物理 RTX の測定ではない。

## 既知の制限・未修正事項

- active Top/Anvil は一つの発達セルのみ。Top は親 + 子最大 2、構造 primitive 最大 8。
  複数セルや anvil の合成 cache は未対応で Direct fallback を使用する。
- Frozen の anvil は trunk の detail を共有する。部位別/異方性 detail、paint #35、
  brush UI #36、および関連 cache #39 は未完成。
- **Custom の明示 crop と detail warp**: 半径 20 の球を envelope `[-5,5]^3` で切り取り、
  base 無効・warp 0 の Custom Recipe として Freeze した場合、warp を 0.001 に増やすと
  `scene_with_frozen_detail()` が envelope を約 `[-22.001,22.001]^3` に拡張し、
  隠れていた領域が現れる。`content_hash` は変わらない。
  明示 crop と拡張可能な sampling bounds を分離するか、明示 crop の拡張を抑止する。
  関連: `src/core/frozen_cloud.cpp`、`src/core/persistence.cpp::frozen_content_hash()`。
  コード確認で見つけた未修正事項。通常の生成 fixture の保守的 bounds とは条件が異なる。
- 描画はまだ滑らかな球/柱の集合に見えるケースがある。機能検証の画像を
  自然な積乱雲の受入済み画像として扱わない。

## ローカル再開情報

作業 root: `/workspace/scratch/59c2b798b94d/Testbed`。
以前の `/workspace/scratch/ed8b99daba06/Testbed` は触らず保持している。
すべての永続 checkpoint は GitHub に保存するため、scratch が無くても再開できる。

標準の CPU 検証:

```sh
cmake --preset cpu-release
cmake --build --preset cpu-release --parallel 2
ctest --preset cpu-release --output-on-failure
```

このコンテナでは `/root/.local/bin` に CMake/Ninja がある。
既存依存を再利用する場合は configure に以下を追加した:

```sh
-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/workspace/scratch/ed8b99daba06/json
-DFETCHCONTENT_SOURCE_DIR_TINYEXR=/workspace/scratch/ed8b99daba06/tinyexr
```

この環境の shell `git push` は資格情報が無く失敗したため、GitHub connector の
tree/blob/commit/ref API で保存した。UTF-8 は CRLF を含む元 bytes を維持し、
PNG/EXR は base64 blob にする。remote を fetch 後、clean status と tree SHA 一致を
確認してから `git reset --keep` で commit identity を合わせた。
Actions の signed artifact URL は Python urllib が 403 でも `curl --location` で取得できた。

並列ビルド中に generated object が 0 byte、実行ファイルの実行 bit が欠ける環境事象が
一度ずつあった。実際の build exit と生成物を確認し、不完全な generated file のみを
再生成する。古い実行ファイルによる成功を新コードの検証と扱わない。
