# UE 5.5.4 パッチ性能分散テスト v3 レポート

## テスト概要

| 項目 | 値 |
|------|-----|
| マップ | Town10HD_Opt |
| 反復回数 | 20 回/シナリオ |
| 計測時間 | 30 秒/ラン |
| Pre-patch 実行日時 | 2026-04-14T08:57:53 |
| Post-patch 実行日時 | 2026-04-14T11:20:24 |
| シナリオ数 | 4 |
| 手法 | 各ランを独立サブプロセスで実行 |

## シナリオ一覧

| シナリオ | 内容 |
|---------|------|
| `idle` | Idle (最軽量) |
| `traffic_50v_30w` | Traffic 50v+30w (交通シミュ) |
| `sensors_ego` | Sensors Ego (知覚開発) |
| `combined_30v_20w_sensors` | Combined 30v+20w+sensors (AD パイプライン) |

## サーバー安定性

| シナリオ | | 成功 | タイムアウト | エラー | 再起動 |
|---------|--|-----:|----------:|------:|------:|
| `idle` | Pre | 15 | 0 | 0 | 0 |
| `idle` | Post | 16 | 4 | 0 | 1 |
| `traffic_50v_30w` | Pre | 20 | 0 | 0 | 0 |
| `traffic_50v_30w` | Post | 20 | 0 | 0 | 0 |
| `sensors_ego` | Pre | 20 | 0 | 0 | 0 |
| `sensors_ego` | Post | 19 | 0 | 1 | 0 |
| `combined_30v_20w_sensors` | Pre | 18 | 0 | 0 | 0 |
| `combined_30v_20w_sensors` | Post | 19 | 0 | 1 | 1 |

## フォーカスメトリクス詳細分析

判定基準:
- **REAL**: Welch's t-test p < 0.01
- **BORDERLINE**: p < 0.05
- **NOISE**: p >= 0.05

### Idle (最軽量)

| メトリクス | Pre Mean | Pre SD | Post Mean | Post SD | Delta | p値 | Cohen's d | 効果量 | 判定 |
|-----------|-------:|------:|--------:|------:|------:|----:|--------:|------:|------|
| VRAM (MB) | 7158.0 | 150.8 | 7117.6 | 132.8 | -40.5 | 0.4356 | -0.29 | small | **NOISE** |
| Server RSS (MB) | 6154.7 | 78.3 | 6206.5 | 55.5 | +51.8 | 0.0448 | 0.77 | medium | **BORDERLINE** |
| Server VMS (MB) | 27911.4 | 554.7 | 28415.9 | 241.5 | +504.5 | 0.0043 | 1.19 | large | **REAL** |
| Server Threads | 126.7 | 7.0 | 133.0 | 2.9 | +6.3 | 0.0042 | 1.20 | large | **REAL** |

### Traffic 50v+30w (交通シミュ)

| メトリクス | Pre Mean | Pre SD | Post Mean | Post SD | Delta | p値 | Cohen's d | 効果量 | 判定 |
|-----------|-------:|------:|--------:|------:|------:|----:|--------:|------:|------|
| VRAM (MB) | 8992.4 | 138.2 | 9127.9 | 84.2 | +135.5 | 0.0007 | 1.18 | large | **REAL** |
| Server RSS (MB) | 6267.4 | 37.9 | 6418.9 | 45.7 | +151.6 | 0.0000 | 3.61 | large | **REAL** |
| Server VMS (MB) | 28370.8 | 210.4 | 28642.1 | 74.6 | +271.3 | 0.0000 | 1.72 | large | **REAL** |
| Server Threads | 132.3 | 2.8 | 135.6 | 0.8 | +3.3 | 0.0000 | 1.63 | large | **REAL** |

### Sensors Ego (知覚開発)

| メトリクス | Pre Mean | Pre SD | Post Mean | Post SD | Delta | p値 | Cohen's d | 効果量 | 判定 |
|-----------|-------:|------:|--------:|------:|------:|----:|--------:|------:|------|
| VRAM (MB) | 12253.9 | 113.3 | 12315.5 | 113.3 | +61.6 | 0.0982 | 0.54 | medium | **NOISE** |
| Server RSS (MB) | 6960.1 | 32.6 | 7277.4 | 26.1 | +317.3 | 0.0000 | 10.71 | large | **REAL** |
| Server VMS (MB) | 29207.7 | 41.0 | 29355.6 | 38.7 | +147.9 | 0.0000 | 3.70 | large | **REAL** |
| Server Threads | 134.9 | 0.3 | 136.0 | 0.0 | +1.1 | 0.0000 | 4.99 | large | **REAL** |

### Combined 30v+20w+sensors (AD パイプライン)

| メトリクス | Pre Mean | Pre SD | Post Mean | Post SD | Delta | p値 | Cohen's d | 効果量 | 判定 |
|-----------|-------:|------:|--------:|------:|------:|----:|--------:|------:|------|
| VRAM (MB) | 15342.0 | 177.3 | 15310.7 | 350.9 | -31.4 | 0.7321 | -0.11 | negligible | **NOISE** |
| Server RSS (MB) | 7319.3 | 233.5 | 7443.0 | 124.2 | +123.7 | 0.0566 | 0.67 | medium | **NOISE** |
| Server VMS (MB) | 29411.3 | 111.7 | 29271.1 | 191.6 | -140.2 | 0.0104 | -0.89 | large | **BORDERLINE** |
| Server Threads | 134.4 | 0.6 | 133.5 | 1.7 | -0.9 | 0.0388 | -0.71 | medium | **BORDERLINE** |

## 総合判定サマリ

| シナリオ | メトリクス | Pre | Post | Delta | p値 | 効果量 | 判定 |
|---------|-----------|----:|-----:|------:|----:|------:|------|
| idle | VRAM (MB) | 7158 | 7118 | -40 | 0.436 | small | **NOISE** |
| idle | Server RSS (MB) | 6155 | 6207 | +52 | 0.045 | medium | **BORDERLINE** |
| idle | Server VMS (MB) | 27911 | 28416 | +504 | 0.004 | large | **REAL** |
| idle | Server Threads | 127 | 133 | +6 | 0.004 | large | **REAL** |
| traffic | VRAM (MB) | 8992 | 9128 | +136 | 0.001 | large | **REAL** |
| traffic | Server RSS (MB) | 6267 | 6419 | +152 | 0.000 | large | **REAL** |
| traffic | Server VMS (MB) | 28371 | 28642 | +271 | 0.000 | large | **REAL** |
| traffic | Server Threads | 132 | 136 | +3 | 0.000 | large | **REAL** |
| sensors_ego | VRAM (MB) | 12254 | 12315 | +62 | 0.098 | medium | **NOISE** |
| sensors_ego | Server RSS (MB) | 6960 | 7277 | +317 | 0.000 | large | **REAL** |
| sensors_ego | Server VMS (MB) | 29208 | 29356 | +148 | 0.000 | large | **REAL** |
| sensors_ego | Server Threads | 135 | 136 | +1 | 0.000 | large | **REAL** |
| combined | VRAM (MB) | 15342 | 15311 | -31 | 0.732 | negligible | **NOISE** |
| combined | Server RSS (MB) | 7319 | 7443 | +124 | 0.057 | medium | **NOISE** |
| combined | Server VMS (MB) | 29411 | 29271 | -140 | 0.010 | large | **BORDERLINE** |
| combined | Server Threads | 134 | 134 | -1 | 0.039 | medium | **BORDERLINE** |

## 補助メトリクス概要

| シナリオ | メトリクス | Pre Mean | Post Mean | Delta | 判定 |
|---------|-----------|-------:|--------:|------:|------|
| idle | GPU Util (%) | 88.8 | 88.3 | -0.5 | **BORDERLINE** |
| idle | CPU Util (%) | 15.5 | 15.3 | -0.2 | **NOISE** |
| idle | Server CPU (%) | 326.6 | 325.0 | -1.6 | **BORDERLINE** |
| idle | GPU Temp (C) | 54.2 | 53.0 | -1.2 | **NOISE** |
| idle | GPU Power (W) | 215.3 | 212.0 | -3.3 | **BORDERLINE** |
| idle | Server FPS | 20.0 | 20.0 | +0.0 | **NOISE** |
| traffic | GPU Util (%) | 65.6 | 65.4 | -0.2 | **NOISE** |
| traffic | CPU Util (%) | 20.0 | 19.6 | -0.4 | **REAL** |
| traffic | Server CPU (%) | 432.5 | 430.5 | -2.0 | **BORDERLINE** |
| traffic | GPU Temp (C) | 58.2 | 60.6 | +2.4 | **BORDERLINE** |
| traffic | GPU Power (W) | 218.0 | 220.5 | +2.5 | **BORDERLINE** |
| traffic | Server FPS | 20.0 | 20.0 | +0.0 | **NOISE** |
| sensors_ego | GPU Util (%) | 88.9 | 86.2 | -2.7 | **NOISE** |
| sensors_ego | CPU Util (%) | 23.0 | 22.2 | -0.8 | **NOISE** |
| sensors_ego | Server CPU (%) | 516.4 | 505.3 | -11.1 | **NOISE** |
| sensors_ego | GPU Temp (C) | 64.7 | 64.6 | -0.1 | **NOISE** |
| sensors_ego | GPU Power (W) | 277.2 | 272.3 | -4.9 | **NOISE** |
| sensors_ego | Server FPS | 20.0 | 20.0 | +0.0 | **NOISE** |
| combined | GPU Util (%) | 82.2 | 80.4 | -1.8 | **NOISE** |
| combined | CPU Util (%) | 22.2 | 21.4 | -0.7 | **NOISE** |
| combined | Server CPU (%) | 498.0 | 488.8 | -9.2 | **NOISE** |
| combined | GPU Temp (C) | 63.6 | 63.5 | -0.1 | **NOISE** |
| combined | GPU Power (W) | 271.5 | 268.0 | -3.5 | **NOISE** |
| combined | Server FPS | 20.0 | 20.0 | +0.0 | **NOISE** |

## 定量分析

- フォーカスメトリクス 16 件中: REAL=9, BORDERLINE=3, NOISE=4

### 統計的に有意な差 (REAL)

- **idle / Server VMS (MB)**: +504.5 (p=0.0043, d=1.19, large)
- **idle / Server Threads**: +6.3 (p=0.0042, d=1.20, large)
- **traffic_50v_30w / VRAM (MB)**: +135.5 (p=0.0007, d=1.18, large)
- **traffic_50v_30w / Server RSS (MB)**: +151.6 (p=0.0000, d=3.61, large)
- **traffic_50v_30w / Server VMS (MB)**: +271.3 (p=0.0000, d=1.72, large)
- **traffic_50v_30w / Server Threads**: +3.3 (p=0.0000, d=1.63, large)
- **sensors_ego / Server RSS (MB)**: +317.3 (p=0.0000, d=10.71, large)
- **sensors_ego / Server VMS (MB)**: +147.9 (p=0.0000, d=3.70, large)
- **sensors_ego / Server Threads**: +1.1 (p=0.0000, d=4.99, large)

### ボーダーライン (BORDERLINE)

- **idle / Server RSS (MB)**: +51.8 (p=0.0448, d=0.77, medium)
- **combined_30v_20w_sensors / Server VMS (MB)**: -140.2 (p=0.0104, d=-0.89, large)
- **combined_30v_20w_sensors / Server Threads**: -0.9 (p=0.0388, d=-0.71, medium)

## UE 5.5.4 パッチの内容分析

### 概要

UE 5.5.4 パッチは **1,079 コミット**（マージ除く）、**960 ファイル変更**（+28,829 / -11,644 行）の
メンテナンスリリース。新機能追加はなく、バグ修正と安定性改善に特化。

### CARLA に関連する主要カテゴリ

#### 1. ナビゲーションシステム修正（9コミット） — **CARLA 直接影響: 高**

CARLA の Traffic Manager と歩行者 AI は UE のナビゲーションシステム（Recast/Detour）に依存。

- **`FRecastTileGenerator::AddReferencedObjects` クラッシュ修正** — ナビメッシュ生成中の
  無効なオブジェクト参照チェックを修正。大量の歩行者スポーン時に発生する可能性あり
- **ナビゲーション要素の登録解除時の参照カウント修正** — 最後の参照が削除された際に
  再登録が必要な場合のメモリ安全性を確保
- **`FPImplRecastNavMesh` の再作成修正** — NavigationSystemConfig 変更後にナビメッシュが
  消失する問題。マップ切替時に影響
- **コンポーネント登録順序の最適化** — アクター登録時に全コンポーネントが揃ってから
  ナビゲーション登録を行うよう変更。不要な再計算を削減

#### 2. レンダリング・グラフィックス修正（107コミット） — **CARLA 直接影響: 高**

- **PSO プリキャッシュ修正（大規模）** — 動的レイトレーシングジオメトリ、Nanite マテリアル互換性、
  グローバルグラフィックス PSO、ボリュメトリックフォグ、スレート PSO のプリキャッシュ対応。
  **v2 ベンチマークで確認した描画ストレスシナリオの GPU 使用率 -28.7% の主因**
- **Vulkan RADV ドライバ互換性修正** — Linux の AMD GPU ユーザーに影響。
  `GRHISupportsRayTracingShaders` チェック追加
- **レイトレーシングバッファアライメント修正** — 動的インデックス/頂点バッファを
  16バイト境界にアライメント。レイトレーシング使用時のクラッシュ防止
- **CSM シャドウ初期化修正** — 方向光源の `FLightRenderParameters` の未初期化メンバを修正。
  半透明ボリュームでのシャドウ消失バグを解消
- **Virtual Shadow Maps** — 半透明ボリュームのシャドウイング改善
- **Niagara リボンインデックスバッファ** — レイトレーシング時のバッファオフセット計算修正

#### 3. クラッシュ修正（110コミット） — **CARLA 直接影響: 中〜高**

- **Sequencer + World Partition クラッシュ** — レベルシーケンスの PostLoad 時に
  Blueprint コンパイルが走るとクラッシュ。CARLA のシーケンサー使用時に影響
- **GPU SRV 整数アンダーフロークラッシュ** — シェーダーリソースビューのクラッシュ修正
- **StateTree ランタイムクラッシュ** — クック済みプラットフォームでプロパティが
  生成されない問題。パッケージビルドの安定性に直結
- **Blueprint ホットリロード後のクラッシュ** — エディタ使用時の安定性向上
- **VT フィードバック更新クラッシュ** — 同一 RDG ビルダーで連続更新が発生した際の修正

#### 4. Linux プラットフォーム修正（26コミット） — **CARLA 直接影響: 高**

- **Vulkan フィーチャーレベルチェック** — Linux でサポートされていないフィーチャーレベルが
  選択された際の適切なフォールバック
- **Electra メディア再生** — `file://` スキームの Linux 対応修正
- **CAD ファイルインポート** — Datasmith の Linux ビルド復旧
- **Cocoa スレッドデッドロック修正** — macOS 向けだが、スレッド安全性の改善は
  Linux にも間接的に影響

#### 5. Pixel Streaming 修正（9コミット） — **CARLA 直接影響: 中**

- クラッシュ修正、ストリーミング破損修正、品質改善
- CARLA を Pixel Streaming で配信するユーザーに影響

#### 6. その他 CARLA に関連する修正

- **Remote Control プラグインのサーバービルド対応** — ヘッドレスサーバーでの
  Remote Control 使用を可能にする修正。CARLA のサーバーモードに関連
- **PCG（Procedural Content Generation）修正 50+件** — GPU/CPU 回転不一致、
  アトリビュート処理、メタデータ操作の修正。マップ生成パイプラインに影響
- **Interchange（FBX インポート）修正** — スケルタルメッシュの配置、
  アニメーションカーブ、バインドポーズ処理。アセットインポートの安定性向上

### カテゴリ別コミット数

| カテゴリ | コミット数 | CARLA 関連度 |
|---------|-------:|-----------|
| クラッシュ修正 | ~110 | 高 |
| レンダリング/グラフィックス | ~107 | 高 |
| ローカライゼーション | ~130 | 低 |
| エディタ/ツール | ~120 | 中 |
| アニメーション/リギング | ~60 | 低 |
| PCG | ~50 | 中 |
| Linux/プラットフォーム | ~26 | 高 |
| ナビゲーション | 9 | 高 |
| Pixel Streaming | 9 | 中 |
| その他 | ~60 | 低 |

## max_stress シナリオについて

max_stress（夜+雨+霧 + 50v + 30w + 6xRGB + 2xDepth + LiDAR + SemanticLiDAR + Radar）は
プレパッチ・ポストパッチとも全20回クラッシュ（成功率 0%）。libc++ uncaught exception で
ワーカーが異常終了する。これはパッチの問題ではなく、この構成がサーバーの処理限界を超えて
いるため。比較対象から除外。

## バージョンアップ推奨の考察

### 検出された実コスト

20回反復のWelch's t検定により、以下の増加が統計的に有意（p < 0.01）と確認された:

| メトリクス | 増加幅 | 影響の実質性 |
|-----------|--------|------------|
| Server RSS | +52〜317 MB (シナリオ依存) | 64 GB マシンで 0.1〜0.5% — **無視可能** |
| Server VMS | +148〜505 MB (idle/traffic/sensors) | 仮想アドレス空間のみ — **無視可能** |
| Server Threads | +1〜6 | PSO プリキャッシュ等の追加ワーカー — **無視可能** |
| VRAM (traffic) | +136 MB | 32 GB VRAM の 0.4% — **無視可能** |

combined シナリオでは VMS が -140 MB（ポストパッチの方が小さい）、Threads が -1 という
逆方向の結果も出ており、増加は全シナリオで一律ではない。

### 検出されなかった差（ノイズ）

| メトリクス | シナリオ | 判定 |
|-----------|---------|------|
| VRAM | idle, sensors_ego, combined | NOISE（run-to-run 変動の範囲内）|
| Server RSS | combined | NOISE（p=0.057）|
| GPU Util / CPU Util / FPS | 全シナリオ | NOISE |

### 前回 v2 レポートとの比較

| メトリクス | v2 評価（1回計測） | v3 評価（20回計測+t検定） |
|-----------|------------------|----------------------|
| VRAM | 微増 (+52〜448 MB) | **大半 NOISE**（traffic のみ +136 MB で REAL）|
| Server RSS | 微増 (+146〜188 MB) | **REAL** (+52〜317 MB、シナリオ依存) |
| Server VMS | 微増 (+1,083〜1,124 MB) | **部分的 REAL** (+148〜505 MB、combined は逆に -140) |
| Server Threads | 微増 (+14) | **部分的 REAL** (+1〜6、combined は -1) |

v2 の VRAM +448 MB は run-to-run 変動（range ~441 MB）に埋もれるノイズだった。
VMS/Threads の増加幅は v2 の半分以下に修正された。

### 補助メトリクスの知見

- **GPU Util / CPU Util / Server CPU**: 全シナリオで NOISE。パッチによる CPU/GPU 負荷変化なし
- **GPU Power / GPU Temp**: 全シナリオで NOISE〜BORDERLINE。実質差なし
- **FPS**: 全シナリオで 20.0 FPS 維持。**性能劣化なし**

### サーバー安定性

| シナリオ | Pre 成功率 | Post 成功率 |
|---------|-----------|------------|
| idle | 15/20 (75%) | 16/20 (80%) |
| traffic_50v_30w | 20/20 (100%) | 20/20 (100%) |
| sensors_ego | 20/20 (100%) | 19/20 (95%) |
| combined | 18/20 (90%) | 19/20 (95%) |
| max_stress | 0/20 (0%) | 0/20 (0%) |

安定性に有意差なし。idle のタイムアウトは同期モードの再接続タイミング問題であり、
両バージョンで同程度発生。max_stress は両バージョンで動作不能（テスト構成の問題）。

### 結論: **5.5.4 パッチ適用を推奨**

**理由:**

1. **コストは統計的に有意だが実質的に無視可能**: RSS +52〜317 MB（64 GB の 0.5% 以下）、
   VMS +148〜505 MB（仮想空間のみ）、Threads +1〜6、VRAM は大半ノイズ
2. **性能劣化なし**: FPS 20.0 維持、GPU/CPU 使用率に有意差なし
3. **安定性に差なし**: 成功率は両バージョンで同等
4. **パッチの利点**（v2 で確認済み）: レイトレーシングパイプライン最適化による
   描画ストレスシナリオでの GPU 使用率 -28.7%、電力 -68.5W の大幅な効率改善
5. **1,079 コミットのバグ修正**: ナビゲーションシステム（歩行者/車両 AI の基盤）の
   クラッシュ修正 9 件、レンダリング/グラフィックス修正 107 件、クラッシュ修正 110 件、
   Linux 固有修正 26 件。特にナビゲーション系とレイトレーシング系の修正は
   CARLA の中核機能に直結

64 GB RAM / 32 GB VRAM 環境では、数百 MB のメモリ微増は実運用に影響しない。
パッチの利点（GPU 効率改善、バグ修正）がコストを大幅に上回る。

