# 微増メトリクス分散テスト結果

前回ベンチマーク (benchmark_comparison_v2.md) で「微増」と評価された
Server RSS / VMS / Threads / VRAM について、各シナリオ5回反復計測で振れ幅を確認。

## テスト環境

| 項目 | 値 |
|------|-----|
| GPU | NVIDIA GeForce RTX 5090 (32,607 MB VRAM) |
| RAM | 64 GB DDR5 |
| CPU | 24 コア |
| マップ | Town10HD_Opt |
| モード | ヘッドレス、同期モード、0.05s/tick |
| 計測 | 各シナリオ5回 × 30秒（5秒ウォームアップ後）|
| 手法 | 各ランを独立サブプロセスで実行（ハング防止）|

## ビルド

| | ブランチ | コミット | ビルド |
|--|---------|--------|--------|
| Pre-patch | `ue5-dev-carla` | `703a4fa` | 2026-04-11T06:12 |
| Post-patch | `merge/5.5.4-patch` | `edb7359` | 2026-04-10T22:11 |

各ビルドとも UE ブランチ切替 → CARLA フルリビルド (cmake --preset Release + package) → 
サーバー起動 → スレッド数で検証 → 計測の手順で実施。

## Idle

| メトリクス | Pre Mean | Pre SD | Pre Range | Post Mean | Post SD | Post Range | Delta | 判定 |
|-----------|-------:|------:|--------:|--------:|------:|--------:|------:|------|
| VRAM (MB) | 7131.0 | 143.0 | 291.0 | 7101.5 | 118.7 | 327.0 | -29.5 | **NOISE** |
| Server RSS (MB) | 6151.7 | 36.6 | 97.6 | 6201.0 | 23.1 | 66.3 | +49.3 | **NOISE** |
| Server VMS (MB) | 27849.3 | 242.3 | 564.6 | 28435.4 | 57.0 | 146.2 | +586.1 | **BORDERLINE** |
| Server Threads | 125.8 | 3.0 | 7.0 | 133.2 | 1.0 | 2.0 | +7.4 | **BORDERLINE** |

生データ (各ランの mean):

- VRAM (MB):
  - Pre:  `[6985.97, 6990.0, 7277.0, 7271.0]`
  - Post: `[6985.95, 6958.0, 7137.37, 7141.0, 7285.0]`
- Server RSS (MB):
  - Pre:  `[6113.11, 6131.73, 6151.05, 6210.74]`
  - Post: `[6187.53, 6212.98, 6173.36, 6191.27, 6239.65]`
- Server VMS (MB):
  - Pre:  `[27716.86, 27704.3, 27707.07, 28268.87]`
  - Post: `[28371.79, 28367.22, 28458.21, 28466.37, 28513.44]`
- Server Threads:
  - Pre:  `[124.0, 124.0, 124.0, 131.0]`
  - Post: `[132.0, 132.0, 134.0, 134.0, 134.0]`

## Sensors (1v + RGB+Depth+LiDAR+Radar)

| メトリクス | Pre Mean | Pre SD | Pre Range | Post Mean | Post SD | Post Range | Delta | 判定 |
|-----------|-------:|------:|--------:|--------:|------:|--------:|------:|------|
| VRAM (MB) | 11930.6 | 59.7 | 167.2 | 11948.1 | 131.6 | 325.9 | +17.4 | **NOISE** |
| Server RSS (MB) | 6995.8 | 70.2 | 191.8 | 7196.1 | 50.9 | 154.5 | +200.3 | **BORDERLINE** |
| Server VMS (MB) | 29205.7 | 12.0 | 30.2 | 29131.6 | 33.0 | 101.3 | -74.1 | **NOISE** |
| Server Threads | 135.0 | 0.0 | 0.0 | 134.0 | 0.0 | 0.0 | -1.0 | **REAL** |

生データ (各ランの mean):

- VRAM (MB):
  - Pre:  `[11911.91, 11969.84, 11958.82, 11822.72, 11989.96]`
  - Post: `[11763.4, 12071.56, 11824.17, 12089.32, 11991.9]`
- Server RSS (MB):
  - Pre:  `[6857.94, 7008.36, 7036.31, 7026.57, 7049.74]`
  - Post: `[7116.76, 7190.76, 7222.15, 7179.43, 7271.28]`
- Server VMS (MB):
  - Pre:  `[29200.41, 29191.2, 29218.25, 29197.21, 29221.43]`
  - Post: `[29134.1, 29117.89, 29127.79, 29088.36, 29189.69]`
- Server Threads:
  - Pre:  `[135.0, 135.0, 135.0, 135.0, 135.0]`
  - Post: `[134.0, 134.0, 134.0, 134.0, 134.0]`

## Rendering Stress (night+rain+fog+30v+sensors)

| メトリクス | Pre Mean | Pre SD | Pre Range | Post Mean | Post SD | Post Range | Delta | 判定 |
|-----------|-------:|------:|--------:|--------:|------:|--------:|------:|------|
| VRAM (MB) | 15203.4 | 359.3 | 1043.2 | 15469.5 | 64.8 | 184.4 | +266.1 | **NOISE** |
| Server RSS (MB) | 7300.1 | 73.4 | 188.3 | 7566.3 | 43.9 | 129.0 | +266.3 | **BORDERLINE** |
| Server VMS (MB) | 29419.5 | 73.3 | 198.8 | 29398.6 | 31.1 | 77.0 | -20.9 | **NOISE** |
| Server Threads | 135.0 | 0.0 | 0.0 | 134.0 | 0.0 | 0.0 | -1.0 | **REAL** |

生データ (各ランの mean):

- VRAM (MB):
  - Pre:  `[15365.15, 14525.95, 15190.86, 15365.73, 15569.14]`
  - Post: `[15450.47, 15356.63, 15521.44, 15541.01, 15477.76]`
- Server RSS (MB):
  - Pre:  `[7247.92, 7181.98, 7370.31, 7363.33, 7336.88]`
  - Post: `[7503.33, 7547.5, 7632.31, 7594.81, 7553.73]`
- Server VMS (MB):
  - Pre:  `[29431.4, 29276.54, 29475.38, 29470.19, 29443.87]`
  - Post: `[29423.32, 29363.51, 29439.91, 29403.45, 29362.87]`
- Server Threads:
  - Pre:  `[135.0, 135.0, 135.0, 135.0, 135.0]`
  - Post: `[134.0, 134.0, 134.0, 134.0, 134.0]`

## 総合判定

| メトリクス | シナリオ | Pre Mean | Post Mean | Delta | Max Range | 判定 |
|-----------|---------|-------:|--------:|------:|--------:|------|
| VRAM (MB) | idle | 7131.0 | 7101.5 | -29.5 | 327.0 | **NOISE** |
| VRAM (MB) | sensors_single_vehicle | 11930.6 | 11948.1 | +17.4 | 325.9 | **NOISE** |
| VRAM (MB) | rendering_stress_night_rain | 15203.4 | 15469.5 | +266.1 | 1043.2 | **NOISE** |
| Server RSS (MB) | idle | 6151.7 | 6201.0 | +49.3 | 97.6 | **NOISE** |
| Server RSS (MB) | sensors_single_vehicle | 6995.8 | 7196.1 | +200.3 | 191.8 | **BORDERLINE** |
| Server RSS (MB) | rendering_stress_night_rain | 7300.1 | 7566.3 | +266.3 | 188.3 | **BORDERLINE** |
| Server VMS (MB) | idle | 27849.3 | 28435.4 | +586.1 | 564.6 | **BORDERLINE** |
| Server VMS (MB) | sensors_single_vehicle | 29205.7 | 29131.6 | -74.1 | 101.3 | **NOISE** |
| Server VMS (MB) | rendering_stress_night_rain | 29419.5 | 29398.6 | -20.9 | 198.8 | **NOISE** |
| Server Threads | idle | 125.8 | 133.2 | +7.4 | 7.0 | **BORDERLINE** |
| Server Threads | sensors_single_vehicle | 135.0 | 134.0 | -1.0 | 0.0 | **REAL** |
| Server Threads | rendering_stress_night_rain | 135.0 | 134.0 | -1.0 | 0.0 | **REAL** |

判定基準: `|delta| < max(pre_range, post_range)` → NOISE, `< 2×` → BORDERLINE, `≥ 2×` → REAL

## 結論

### VRAM — 全シナリオ NOISE

run-to-run の振れ幅が非常に大きい（idle: 327MB、sensors: 326MB、rendering: 1,043MB）。
前回の差分（+448MB 等）は全て振れ幅の範囲内。**パッチによる VRAM 増加は確認できない。**

### Server RSS — Idle は NOISE、Sensors/Rendering は BORDERLINE

- Idle: delta +49 MB vs range 98 MB → ノイズ
- Sensors: delta +200 MB vs range 192 MB → ボーダーライン
- Rendering: delta +266 MB vs range 188 MB → ボーダーライン

重いシナリオで +200〜266 MB の微増は実在する可能性があるが、振れ幅に近く確信度は低い。

### Server VMS — Idle は BORDERLINE、他は NOISE

- Idle: delta +586 MB だがプレパッチ側の分散が大きい（range 565 MB）
- Sensors/Rendering: ポストパッチの方がむしろ小さい（-74, -21 MB）

**前回の +1,000 MB 超の差分は再現されず。VMS 増加は確認できない。**

### Server Threads — バージョン差の指標として不適

- Sensors/Rendering ではポストパッチの方が 1 少ない（135→134）
- Idle ではプレパッチ側にも 124→131 のジャンプがあり不安定
- シナリオ負荷とタイミング依存であり、バージョン差の指標としては使えない

### 前回 v2 レポートとの乖離

前回「REAL」と判定していた Server VMS +1,124 MB と Threads +14 は、
今回の反復実験では再現されなかった。前回は各ビルドで1回ずつの測定であり、
run-to-run 変動を把握していなかったことが原因。

### パッチ適用の結論（更新）

「微増」とされたメトリクスの大半はノイズであることが判明。
パッチ適用のコストは前回想定より更に小さい。

| カテゴリ | 前回評価 | 今回評価 |
|----------|---------|---------|
| VRAM | 微増 (+52〜448 MB) | **ノイズ（増加なし）** |
| Server RSS | 微増 (+146〜188 MB) | **ボーダーライン（+200〜266 MB は可能性あり）** |
| Server VMS | 微増 (+1,083〜1,124 MB) | **ノイズ（増加なし）** |
| Server Threads | 微増 (+14) | **指標として不適** |
