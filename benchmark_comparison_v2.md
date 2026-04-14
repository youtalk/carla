# CARLA Performance Benchmark Comparison

## System

| Item | Value |
|------|-------|
| GPU | NVIDIA GeForce RTX 5090 |
| VRAM | 32607.0 MB |
| RAM | 63746.0 MB |
| CPU | 24 physical / 24 logical |
| Map | Town10HD_Opt |
| Pre-patch run | 2026-04-09T17:01:50.244603 (UE-5.5.0-pre-patch) |
| Post-patch run | 2026-04-09T15:58:38.890376 (UE-5.5.4-post-patch) |

## 01_idle

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 89.2 | 88.6 | -0.6 :arrow_down: | -0.7% |
| VRAM Used (MB) | 7000.7 | 6971.9 | -28.8 :arrow_down: | -0.4% |
| CPU Utilization (%) | 17.9 | 18.2 | +0.3 :arrow_up: | +1.9% |
| System RAM Used (MB) | 10491.4 | 11529.7 | +1038.3 :arrow_up: | +9.9% |
| Server RSS (MB) | 6045.8 | 6233.5 | +187.8 :arrow_up: | +3.1% |
| Server VMS (MB) | 27291.5 | 28415.5 | +1124.0 :arrow_up: | +4.1% |
| Server CPU (%) | 329.9 | 335.4 | +5.5 :arrow_up: | +1.7% |
| Server Threads | 119.0 | 133.0 | +14.0 :arrow_up: | +11.8% |
| GPU Temperature (C) | 51.5 | 49.7 | -1.8 :arrow_down: | -3.5% |
| GPU Power Draw (W) | 209.7 | 209.6 | -0.1 :arrow_down: | -0.0% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 02_light_traffic_20v

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 88.9 | 88.5 | -0.4 :arrow_down: | -0.4% |
| VRAM Used (MB) | 7683.6 | 7698.0 | +14.4 :arrow_up: | +0.2% |
| CPU Utilization (%) | 22.6 | 22.9 | +0.3 :arrow_up: | +1.4% |
| System RAM Used (MB) | 10502.8 | 11451.4 | +948.5 :arrow_up: | +9.0% |
| Server RSS (MB) | 5958.8 | 6104.9 | +146.1 :arrow_up: | +2.5% |
| Server VMS (MB) | 27209.1 | 28296.4 | +1087.3 :arrow_up: | +4.0% |
| Server CPU (%) | 442.3 | 447.9 | +5.5 :arrow_up: | +1.2% |
| Server Threads | 119.0 | 133.0 | +14.0 :arrow_up: | +11.8% |
| GPU Temperature (C) | 55.1 | 53.9 | -1.2 :arrow_down: | -2.2% |
| GPU Power Draw (W) | 231.8 | 231.5 | -0.2 :arrow_down: | -0.1% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 03_heavy_traffic_50v_30w

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 66.7 | 65.6 | -1.1 :arrow_down: | -1.6% |
| VRAM Used (MB) | 8745.2 | 8692.6 | -52.7 :arrow_down: | -0.6% |
| CPU Utilization (%) | 21.7 | 21.9 | +0.1 :arrow_up: | +0.5% |
| System RAM Used (MB) | 10479.8 | 11400.5 | +920.7 :arrow_up: | +8.8% |
| Server RSS (MB) | 6074.3 | 6235.9 | +161.6 :arrow_up: | +2.7% |
| Server VMS (MB) | 27335.0 | 28417.7 | +1082.7 :arrow_up: | +4.0% |
| Server CPU (%) | 426.6 | 429.6 | +3.0 :arrow_up: | +0.7% |
| Server Threads | 119.0 | 133.0 | +14.0 :arrow_up: | +11.8% |
| GPU Temperature (C) | 56.8 | 55.8 | -1.0 :arrow_down: | -1.7% |
| GPU Power Draw (W) | 214.7 | 210.7 | -4.0 :arrow_down: | -1.9% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 04_sensors_single_vehicle

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 89.1 | 82.4 | -6.7 :arrow_down: | -7.5% |
| VRAM Used (MB) | 13149.5 | 13597.7 | +448.2 :arrow_up: | +3.4% |
| CPU Utilization (%) | 23.8 | 23.4 | -0.4 :arrow_down: | -1.9% |
| System RAM Used (MB) | 10497.5 | 11608.8 | +1111.3 :arrow_up: | +10.6% |
| Server RSS (MB) | 6833.4 | 6979.0 | +145.6 :arrow_up: | +2.1% |
| Server VMS (MB) | 28063.5 | 29153.2 | +1089.6 :arrow_up: | +3.9% |
| Server CPU (%) | 489.6 | 477.2 | -12.4 :arrow_down: | -2.5% |
| Server Threads | 119.0 | 133.0 | +14.0 :arrow_up: | +11.8% |
| GPU Temperature (C) | 60.7 | 59.4 | -1.3 :arrow_down: | -2.1% |
| GPU Power Draw (W) | 268.7 | 255.8 | -13.0 :arrow_down: | -4.8% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 05_combined_30v_20w_sensors

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 91.2 | 92.0 | +0.8 :arrow_up: | +0.9% |
| VRAM Used (MB) | 13610.8 | 13662.7 | +51.9 :arrow_up: | +0.4% |
| CPU Utilization (%) | 26.9 | 27.2 | +0.2 :arrow_up: | +0.9% |
| System RAM Used (MB) | 10498.6 | 11612.5 | +1113.8 :arrow_up: | +10.6% |
| Server RSS (MB) | 6800.9 | 6967.8 | +166.9 :arrow_up: | +2.5% |
| Server VMS (MB) | 28038.7 | 29145.6 | +1106.9 :arrow_up: | +4.0% |
| Server CPU (%) | 559.2 | 565.5 | +6.3 :arrow_up: | +1.1% |
| Server Threads | 119.0 | 133.0 | +14.0 :arrow_up: | +11.8% |
| GPU Temperature (C) | 64.4 | 63.4 | -1.1 :arrow_down: | -1.7% |
| GPU Power Draw (W) | 296.9 | 296.6 | -0.3 :arrow_down: | -0.1% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 06_rendering_stress_night_rain

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 88.4 | 59.8 | -28.7 :arrow_down: | -32.4% |
| VRAM Used (MB) | 13867.5 | 14149.6 | +282.1 :arrow_up: | +2.0% |
| CPU Utilization (%) | 26.6 | 17.9 | -8.7 :arrow_down: | -32.7% |
| System RAM Used (MB) | 10732.0 | 11628.3 | +896.3 :arrow_up: | +8.3% |
| Server RSS (MB) | 6827.4 | 6684.8 | -142.6 :arrow_down: | -2.1% |
| Server VMS (MB) | 28064.9 | 28854.6 | +789.7 :arrow_up: | +2.8% |
| Server CPU (%) | 551.6 | 347.9 | -203.7 :arrow_down: | -36.9% |
| Server Threads | 119.0 | 133.0 | +14.0 :arrow_up: | +11.8% |
| GPU Temperature (C) | 66.0 | 62.9 | -3.1 :arrow_down: | -4.7% |
| GPU Power Draw (W) | 295.6 | 227.1 | -68.5 :arrow_down: | -23.2% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## Summary: Key Metrics Across All Scenarios

| Scenario | VRAM (MB) Pre | VRAM (MB) Post | VRAM (MB) Delta | RAM (MB) Pre | RAM (MB) Post | RAM (MB) Delta | Server RSS (MB) Pre | Server RSS (MB) Post | Server RSS (MB) Delta | GPU % Pre | GPU % Post | GPU % Delta | CPU % Pre | CPU % Post | CPU % Delta | FPS Pre | FPS Post | FPS Delta |
|----------|----:|-----:|------:|----:|-----:|------:|----:|-----:|------:|----:|-----:|------:|----:|-----:|------:|----:|-----:|------:|
| 01_idle | 7001 | 6972 | -29 | 10491 | 11530 | +1038 | 6046 | 6234 | +188 | 89 | 89 | -1 | 18 | 18 | +0 | 20 | 20 | +0 |
| 02_light_traffic_20v | 7684 | 7698 | +14 | 10503 | 11451 | +949 | 5959 | 6105 | +146 | 89 | 89 | -0 | 23 | 23 | +0 | 20 | 20 | +0 |
| 03_heavy_traffic_50v_30w | 8745 | 8693 | -53 | 10480 | 11400 | +921 | 6074 | 6236 | +162 | 67 | 66 | -1 | 22 | 22 | +0 | 20 | 20 | +0 |
| 04_sensors_single_vehicle | 13150 | 13598 | +448 | 10497 | 11609 | +1111 | 6833 | 6979 | +146 | 89 | 82 | -7 | 24 | 23 | -0 | 20 | 20 | +0 |
| 05_combined_30v_20w_sensors | 13611 | 13663 | +52 | 10499 | 11612 | +1114 | 6801 | 6968 | +167 | 91 | 92 | +1 | 27 | 27 | +0 | 20 | 20 | +0 |
| 06_rendering_stress_night_rain | 13867 | 14150 | +282 | 10732 | 11628 | +896 | 6827 | 6685 | -143 | 88 | 60 | -29 | 27 | 18 | -9 | 20 | 20 | +0 |
