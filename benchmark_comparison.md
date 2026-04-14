# CARLA Performance Benchmark Comparison

## System

| Item | Value |
|------|-------|
| GPU | NVIDIA GeForce RTX 5090 |
| VRAM | 32607.0 MB |
| RAM | 63746.0 MB |
| CPU | 24 physical / 24 logical |
| Map | Town10HD_Opt |
| Pre-patch run | 2026-04-09T14:02:18.235417 (UE-5.5.0-pre-patch) |
| Post-patch run | 2026-04-09T15:10:22.924617 (UE-5.5.4-post-patch) |

## 01_idle

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 89.1 | 89.0 | -0.0 :arrow_down: | -0.0% |
| VRAM Used (MB) | 6975.9 | 6912.0 | -63.9 :arrow_down: | -0.9% |
| CPU Utilization (%) | 16.2 | 23.8 | +7.6 :arrow_up: | +46.7% |
| System RAM Used (MB) | 10000.3 | 11907.7 | +1907.4 :arrow_up: | +19.1% |
| Server RSS (MB) | 1.9 | 6397.3 | +6395.4 :arrow_up: | +342000.5% |
| Server VMS (MB) | 2.7 | 28510.0 | +28507.3 :arrow_up: | +1040412.4% |
| Server CPU (%) | 0.0 | 0.0 | +0.0 | N/A |
| Server Threads | 1.0 | 135.0 | +134.0 :arrow_up: | +13400.0% |
| GPU Temperature (C) | 49.2 | 52.5 | +3.4 :arrow_up: | +6.8% |
| GPU Power Draw (W) | 210.7 | 214.2 | +3.5 :arrow_up: | +1.7% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 02_light_traffic_20v

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 88.0 | 88.9 | +0.9 :arrow_up: | +1.0% |
| VRAM Used (MB) | 7725.0 | 7638.8 | -86.2 :arrow_down: | -1.1% |
| CPU Utilization (%) | 20.7 | 28.4 | +7.7 :arrow_up: | +37.2% |
| System RAM Used (MB) | 10118.3 | 11904.0 | +1785.7 :arrow_up: | +17.6% |
| Server RSS (MB) | 1.9 | 6305.1 | +6303.2 :arrow_up: | +337070.0% |
| Server VMS (MB) | 2.7 | 28369.2 | +28366.5 :arrow_up: | +1035271.9% |
| Server CPU (%) | 0.0 | 0.0 | +0.0 | N/A |
| Server Threads | 1.0 | 135.0 | +134.0 :arrow_up: | +13400.0% |
| GPU Temperature (C) | 53.2 | 55.9 | +2.7 :arrow_up: | +5.1% |
| GPU Power Draw (W) | 231.2 | 232.9 | +1.7 :arrow_up: | +0.7% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 03_heavy_traffic_50v_30w

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 66.7 | 64.4 | -2.3 :arrow_down: | -3.4% |
| VRAM Used (MB) | 8714.9 | 8632.3 | -82.6 :arrow_down: | -0.9% |
| CPU Utilization (%) | 20.1 | 26.9 | +6.8 :arrow_up: | +33.6% |
| System RAM Used (MB) | 10065.4 | 11872.6 | +1807.3 :arrow_up: | +18.0% |
| Server RSS (MB) | 1.9 | 6449.8 | +6447.9 :arrow_up: | +344808.6% |
| Server VMS (MB) | 2.7 | 28530.3 | +28527.6 :arrow_up: | +1041152.2% |
| Server CPU (%) | 0.0 | 0.0 | +0.0 | N/A |
| Server Threads | 1.0 | 135.0 | +134.0 :arrow_up: | +13400.0% |
| GPU Temperature (C) | 55.6 | 57.6 | +1.9 :arrow_up: | +3.5% |
| GPU Power Draw (W) | 217.7 | 212.3 | -5.4 :arrow_down: | -2.5% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 04_sensors_single_vehicle

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 95.1 | 79.7 | -15.4 :arrow_down: | -16.2% |
| VRAM Used (MB) | 11172.5 | 13614.1 | +2441.6 :arrow_up: | +21.9% |
| CPU Utilization (%) | 24.7 | 28.6 | +3.9 :arrow_up: | +15.8% |
| System RAM Used (MB) | 10391.3 | 12029.2 | +1637.8 :arrow_up: | +15.8% |
| Server RSS (MB) | 1.9 | 7291.9 | +7290.0 :arrow_up: | +389840.6% |
| Server VMS (MB) | 2.7 | 29263.0 | +29260.2 :arrow_up: | +1067891.6% |
| Server CPU (%) | 0.0 | 0.0 | +0.0 | N/A |
| Server Threads | 1.0 | 135.0 | +134.0 :arrow_up: | +13400.0% |
| GPU Temperature (C) | 60.6 | 60.7 | +0.1 :arrow_up: | +0.2% |
| GPU Power Draw (W) | 287.8 | 252.0 | -35.8 :arrow_down: | -12.4% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 05_combined_30v_20w_sensors

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 87.0 | 89.9 | +3.0 :arrow_up: | +3.4% |
| VRAM Used (MB) | 14937.2 | 13650.6 | -1286.5 :arrow_down: | -8.6% |
| CPU Utilization (%) | 24.5 | 34.1 | +9.6 :arrow_up: | +39.0% |
| System RAM Used (MB) | 10693.0 | 12431.3 | +1738.3 :arrow_up: | +16.3% |
| Server RSS (MB) | 1.9 | 7387.2 | +7385.4 :arrow_up: | +394940.1% |
| Server VMS (MB) | 2.7 | 29234.5 | +29231.7 :arrow_up: | +1066851.5% |
| Server CPU (%) | 0.0 | 0.0 | +0.0 | N/A |
| Server Threads | 1.0 | 135.0 | +134.0 :arrow_up: | +13400.0% |
| GPU Temperature (C) | 63.4 | 64.4 | +1.0 :arrow_up: | +1.6% |
| GPU Power Draw (W) | 280.2 | 293.4 | +13.2 :arrow_up: | +4.7% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## 06_rendering_stress_night_rain

| Metric | UE-5.5.0-pre-patch | UE-5.5.4-post-patch | Delta | Delta % |
|--------|------------|-------------|-------|---------|
| GPU Utilization (%) | 85.5 | 89.9 | +4.4 :arrow_up: | +5.1% |
| VRAM Used (MB) | 15178.0 | 13762.0 | -1416.0 :arrow_down: | -9.3% |
| CPU Utilization (%) | 23.4 | 33.2 | +9.8 :arrow_up: | +42.0% |
| System RAM Used (MB) | 10658.1 | 12452.2 | +1794.1 :arrow_up: | +16.8% |
| Server RSS (MB) | 1.9 | 7370.7 | +7368.8 :arrow_up: | +394053.5% |
| Server VMS (MB) | 2.7 | 29209.7 | +29206.9 :arrow_up: | +1065945.6% |
| Server CPU (%) | 0.0 | 0.0 | +0.0 | N/A |
| Server Threads | 1.0 | 135.0 | +134.0 :arrow_up: | +13400.0% |
| GPU Temperature (C) | 65.2 | 66.4 | +1.2 :arrow_up: | +1.9% |
| GPU Power Draw (W) | 283.2 | 297.1 | +13.9 :arrow_up: | +4.9% |
| Server FPS | 20.0 | 20.0 | +0.0 | +0.0% |

## Summary: Key Metrics Across All Scenarios

| Scenario | VRAM (MB) Pre | VRAM (MB) Post | VRAM (MB) Delta | RAM (MB) Pre | RAM (MB) Post | RAM (MB) Delta | Server RSS (MB) Pre | Server RSS (MB) Post | Server RSS (MB) Delta | GPU % Pre | GPU % Post | GPU % Delta | CPU % Pre | CPU % Post | CPU % Delta | FPS Pre | FPS Post | FPS Delta |
|----------|----:|-----:|------:|----:|-----:|------:|----:|-----:|------:|----:|-----:|------:|----:|-----:|------:|----:|-----:|------:|
| 01_idle | 6976 | 6912 | -64 | 10000 | 11908 | +1907 | 2 | 6397 | +6395 | 89 | 89 | -0 | 16 | 24 | +8 | 20 | 20 | +0 |
| 02_light_traffic_20v | 7725 | 7639 | -86 | 10118 | 11904 | +1786 | 2 | 6305 | +6303 | 88 | 89 | +1 | 21 | 28 | +8 | 20 | 20 | +0 |
| 03_heavy_traffic_50v_30w | 8715 | 8632 | -83 | 10065 | 11873 | +1807 | 2 | 6450 | +6448 | 67 | 64 | -2 | 20 | 27 | +7 | 20 | 20 | +0 |
| 04_sensors_single_vehicle | 11172 | 13614 | +2442 | 10391 | 12029 | +1638 | 2 | 7292 | +7290 | 95 | 80 | -15 | 25 | 29 | +4 | 20 | 20 | +0 |
| 05_combined_30v_20w_sensors | 14937 | 13651 | -1287 | 10693 | 12431 | +1738 | 2 | 7387 | +7385 | 87 | 90 | +3 | 25 | 34 | +10 | 20 | 20 | +0 |
| 06_rendering_stress_night_rain | 15178 | 13762 | -1416 | 10658 | 12452 | +1794 | 2 | 7371 | +7369 | 85 | 90 | +4 | 23 | 33 | +10 | 20 | 20 | +0 |
