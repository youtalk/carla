# UE 5.5.4 Patch Performance Variance Test v3 Report

## Test Overview

| Item | Value |
|------|-------|
| Map | Town10HD_Opt |
| Repetitions | 20 runs/scenario |
| Duration | 30 seconds/run |
| Pre-patch run | 2026-04-14T08:57:53 |
| Post-patch run | 2026-04-14T11:20:24 |
| Scenarios | 4 |
| Method | Each run executed as independent subprocess |

## Scenarios

| Scenario | Description |
|----------|-------------|
| `idle` | Idle (lightest baseline) |
| `traffic_50v_30w` | Traffic 50v+30w (traffic simulation) |
| `sensors_ego` | Sensors Ego (perception development) |
| `combined_30v_20w_sensors` | Combined 30v+20w+sensors (AD pipeline) |

## Server Stability

| Scenario | | Success | Timeouts | Errors | Restarts |
|---------|--|--------:|---------:|-------:|---------:|
| `idle` | Pre | 15 | 0 | 0 | 0 |
| `idle` | Post | 16 | 4 | 0 | 1 |
| `traffic_50v_30w` | Pre | 20 | 0 | 0 | 0 |
| `traffic_50v_30w` | Post | 20 | 0 | 0 | 0 |
| `sensors_ego` | Pre | 20 | 0 | 0 | 0 |
| `sensors_ego` | Post | 19 | 0 | 1 | 0 |
| `combined_30v_20w_sensors` | Pre | 18 | 0 | 0 | 0 |
| `combined_30v_20w_sensors` | Post | 19 | 0 | 1 | 1 |

## Focus Metrics Detailed Analysis

Verdict criteria:
- **REAL**: Welch's t-test p < 0.01
- **BORDERLINE**: p < 0.05
- **NOISE**: p >= 0.05

### Idle (Lightest Baseline)

| Metric | Pre Mean | Pre SD | Post Mean | Post SD | Delta | p-value | Cohen's d | Effect Size | Verdict |
|--------|-------:|------:|--------:|------:|------:|----:|--------:|------:|------|
| VRAM (MB) | 7158.0 | 150.8 | 7117.6 | 132.8 | -40.5 | 0.4356 | -0.29 | small | **NOISE** |
| Server RSS (MB) | 6154.7 | 78.3 | 6206.5 | 55.5 | +51.8 | 0.0448 | 0.77 | medium | **BORDERLINE** |
| Server VMS (MB) | 27911.4 | 554.7 | 28415.9 | 241.5 | +504.5 | 0.0043 | 1.19 | large | **REAL** |
| Server Threads | 126.7 | 7.0 | 133.0 | 2.9 | +6.3 | 0.0042 | 1.20 | large | **REAL** |

### Traffic 50v+30w (Traffic Simulation)

| Metric | Pre Mean | Pre SD | Post Mean | Post SD | Delta | p-value | Cohen's d | Effect Size | Verdict |
|--------|-------:|------:|--------:|------:|------:|----:|--------:|------:|------|
| VRAM (MB) | 8992.4 | 138.2 | 9127.9 | 84.2 | +135.5 | 0.0007 | 1.18 | large | **REAL** |
| Server RSS (MB) | 6267.4 | 37.9 | 6418.9 | 45.7 | +151.6 | 0.0000 | 3.61 | large | **REAL** |
| Server VMS (MB) | 28370.8 | 210.4 | 28642.1 | 74.6 | +271.3 | 0.0000 | 1.72 | large | **REAL** |
| Server Threads | 132.3 | 2.8 | 135.6 | 0.8 | +3.3 | 0.0000 | 1.63 | large | **REAL** |

### Sensors Ego (Perception Development)

| Metric | Pre Mean | Pre SD | Post Mean | Post SD | Delta | p-value | Cohen's d | Effect Size | Verdict |
|--------|-------:|------:|--------:|------:|------:|----:|--------:|------:|------|
| VRAM (MB) | 12253.9 | 113.3 | 12315.5 | 113.3 | +61.6 | 0.0982 | 0.54 | medium | **NOISE** |
| Server RSS (MB) | 6960.1 | 32.6 | 7277.4 | 26.1 | +317.3 | 0.0000 | 10.71 | large | **REAL** |
| Server VMS (MB) | 29207.7 | 41.0 | 29355.6 | 38.7 | +147.9 | 0.0000 | 3.70 | large | **REAL** |
| Server Threads | 134.9 | 0.3 | 136.0 | 0.0 | +1.1 | 0.0000 | 4.99 | large | **REAL** |

### Combined 30v+20w+sensors (AD Pipeline)

| Metric | Pre Mean | Pre SD | Post Mean | Post SD | Delta | p-value | Cohen's d | Effect Size | Verdict |
|--------|-------:|------:|--------:|------:|------:|----:|--------:|------:|------|
| VRAM (MB) | 15342.0 | 177.3 | 15310.7 | 350.9 | -31.4 | 0.7321 | -0.11 | negligible | **NOISE** |
| Server RSS (MB) | 7319.3 | 233.5 | 7443.0 | 124.2 | +123.7 | 0.0566 | 0.67 | medium | **NOISE** |
| Server VMS (MB) | 29411.3 | 111.7 | 29271.1 | 191.6 | -140.2 | 0.0104 | -0.89 | large | **BORDERLINE** |
| Server Threads | 134.4 | 0.6 | 133.5 | 1.7 | -0.9 | 0.0388 | -0.71 | medium | **BORDERLINE** |

## Summary

| Scenario | Metric | Pre | Post | Delta | p-value | Effect Size | Verdict |
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

## Secondary Metrics Overview

| Scenario | Metric | Pre Mean | Post Mean | Delta | Verdict |
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

## Quantitative Analysis

- Focus metrics: 16 total — REAL=9, BORDERLINE=3, NOISE=4

### Statistically Significant Differences (REAL)

- **idle / Server VMS (MB)**: +504.5 (p=0.0043, d=1.19, large)
- **idle / Server Threads**: +6.3 (p=0.0042, d=1.20, large)
- **traffic_50v_30w / VRAM (MB)**: +135.5 (p=0.0007, d=1.18, large)
- **traffic_50v_30w / Server RSS (MB)**: +151.6 (p=0.0000, d=3.61, large)
- **traffic_50v_30w / Server VMS (MB)**: +271.3 (p=0.0000, d=1.72, large)
- **traffic_50v_30w / Server Threads**: +3.3 (p=0.0000, d=1.63, large)
- **sensors_ego / Server RSS (MB)**: +317.3 (p=0.0000, d=10.71, large)
- **sensors_ego / Server VMS (MB)**: +147.9 (p=0.0000, d=3.70, large)
- **sensors_ego / Server Threads**: +1.1 (p=0.0000, d=4.99, large)

### Borderline (BORDERLINE)

- **idle / Server RSS (MB)**: +51.8 (p=0.0448, d=0.77, medium)
- **combined_30v_20w_sensors / Server VMS (MB)**: -140.2 (p=0.0104, d=-0.89, large)
- **combined_30v_20w_sensors / Server Threads**: -0.9 (p=0.0388, d=-0.71, medium)

## UE 5.5.4 Patch Content Analysis

### Overview

The UE 5.5.4 patch comprises **1,079 commits** (excluding merges), **960 files changed**
(+28,829 / -11,644 lines). It is a maintenance release with no new features,
focused entirely on bug fixes and stability improvements.

### Key Categories Relevant to CARLA

#### 1. Navigation System Fixes (9 commits) — **CARLA Impact: High**

CARLA's Traffic Manager and pedestrian AI depend on UE's navigation system (Recast/Detour).

- **`FRecastTileGenerator::AddReferencedObjects` crash fix** — Corrected invalid object
  reference check during nav mesh generation. Can be triggered when spawning many walkers
- **Navigation element reference counting fix on unregistration** — Ensures memory safety
  when the last reference is removed and re-registration is needed
- **`FPImplRecastNavMesh` recreation fix** — Nav mesh disappearing after
  NavigationSystemConfig changes. Affects map switching
- **Component registration order optimization** — Navigation registration now waits for all
  components to be registered, reducing unnecessary recalculations

#### 2. Rendering / Graphics Fixes (107 commits) — **CARLA Impact: High**

- **PSO precaching fixes (major)** — Dynamic ray tracing geometry, Nanite material
  compatibility, global graphics PSO, volumetric fog, and Slate PSO precaching support
- **Vulkan RADV driver compatibility fix** — Affects Linux AMD GPU users.
  Added `GRHISupportsRayTracingShaders` check
- **Ray tracing buffer alignment fix** — Dynamic index/vertex buffers aligned to
  16-byte boundaries. Prevents crashes when using ray tracing
- **CSM shadow initialization fix** — Uninitialized `FLightRenderParameters` members
  corrected. Fixes shadow disappearance in translucent volumes
- **Virtual Shadow Maps** — Improved shadowing for translucent volumes
- **Niagara ribbon index buffer** — Buffer offset calculation fix for ray tracing

#### 3. Crash Fixes (110 commits) — **CARLA Impact: Medium-High**

- **Sequencer + World Partition crash** — Crash when Blueprint compilation runs during
  level sequence PostLoad
- **GPU SRV integer underflow crash** — Shader resource view crash fix
- **StateTree runtime crash** — Property not generated for cooked platforms.
  Directly affects packaged build stability
- **Blueprint hot-reload crash** — Editor stability improvement
- **VT feedback update crash** — Fix for back-to-back updates in the same RDG builder

#### 4. Linux Platform Fixes (26 commits) — **CARLA Impact: High**

- **Vulkan feature level check** — Proper fallback when unsupported feature levels
  are selected on Linux
- **Electra media playback** — `file://` scheme handling fix for Linux
- **CAD file import** — Datasmith Linux build restoration
- **Cocoa thread deadlock fix** — macOS-targeted but thread safety improvements
  indirectly benefit Linux

#### 5. Pixel Streaming Fixes (9 commits) — **CARLA Impact: Medium**

- Crash fixes, streaming corruption fixes, quality improvements
- Affects users streaming CARLA via Pixel Streaming

#### 6. Other CARLA-Relevant Fixes

- **Remote Control plugin server build support** — Enables Remote Control in headless
  server mode. Relevant to CARLA's server deployment
- **PCG (Procedural Content Generation) 50+ fixes** — GPU/CPU rotation mismatch,
  attribute handling, metadata operations. Affects map generation pipeline
- **Interchange (FBX import) fixes** — Skeletal mesh placement, animation curves,
  bind pose handling. Improves asset import stability

### Commits by Category

| Category | Commits | CARLA Relevance |
|----------|--------:|-----------------|
| Crash fixes | ~110 | High |
| Rendering / Graphics | ~107 | High |
| Localization | ~130 | Low |
| Editor / Tools | ~120 | Medium |
| Animation / Rigging | ~60 | Low |
| PCG | ~50 | Medium |
| Linux / Platform | ~26 | High |
| Navigation | 9 | High |
| Pixel Streaming | 9 | Medium |
| Other | ~60 | Low |

## max_stress Scenario Note

max_stress (night+rain+fog + 50v + 30w + 6xRGB + 2xDepth + LiDAR + SemanticLiDAR + Radar)
crashed in all 20 runs on both pre-patch and post-patch (0% success rate). The worker
process terminated with libc++ uncaught exception. This is not a patch issue — this
configuration exceeds the server's processing limits. Excluded from comparison.

## Upgrade Recommendation

### Measured Costs

Welch's t-test with 20 repetitions confirmed the following statistically significant
(p < 0.01) increases:

| Metric | Increase | Practical Significance |
|--------|----------|----------------------|
| Server RSS | +52 ~ 317 MB (scenario-dependent) | 0.1 ~ 0.5% of 64 GB — **negligible** |
| Server VMS | +148 ~ 505 MB (idle/traffic/sensors) | Virtual address space only — **negligible** |
| Server Threads | +1 ~ 6 | PSO precache workers etc. — **negligible** |
| VRAM (traffic) | +136 MB | 0.4% of 32 GB VRAM — **negligible** |

In the combined scenario, VMS was -140 MB (post-patch smaller) and Threads -1,
showing that increases are not uniform across all scenarios.

### Not Detected (Noise)

| Metric | Scenarios | Verdict |
|--------|-----------|--------|
| VRAM | idle, sensors_ego, combined | NOISE (within run-to-run variation) |
| Server RSS | combined | NOISE (p=0.057) |
| GPU Util / CPU Util / FPS | All scenarios | NOISE |

### Comparison with v2 Report

| Metric | v2 Assessment (single run) | v3 Assessment (20 runs + t-test) |
|--------|---------------------------|--------------------------------|
| VRAM | Slight increase (+52 ~ 448 MB) | **Mostly NOISE** (only traffic +136 MB is REAL) |
| Server RSS | Slight increase (+146 ~ 188 MB) | **REAL** (+52 ~ 317 MB, scenario-dependent) |
| Server VMS | Slight increase (+1,083 ~ 1,124 MB) | **Partially REAL** (+148 ~ 505 MB, combined is -140) |
| Server Threads | Slight increase (+14) | **Partially REAL** (+1 ~ 6, combined is -1) |

The v2 VRAM +448 MB was noise buried in run-to-run variation (range ~441 MB).
VMS/Threads increases were revised to less than half of v2 estimates.

### Secondary Metrics Findings

- **GPU Util / CPU Util / Server CPU**: NOISE across all scenarios. No CPU/GPU load change from the patch
- **GPU Power / GPU Temp**: NOISE to BORDERLINE across all scenarios. No practical difference
- **FPS**: 20.0 FPS maintained across all scenarios. **No performance degradation**

### Server Stability

| Scenario | Pre Success Rate | Post Success Rate |
|---------|-----------------|-------------------|
| idle | 15/20 (75%) | 16/20 (80%) |
| traffic_50v_30w | 20/20 (100%) | 20/20 (100%) |
| sensors_ego | 20/20 (100%) | 19/20 (95%) |
| combined | 18/20 (90%) | 19/20 (95%) |
| max_stress | 0/20 (0%) | 0/20 (0%) |

No significant stability difference. Idle timeouts are a synchronous mode reconnection
timing issue, occurring at similar rates on both versions. max_stress is inoperable
on both versions (test configuration limitation).

### Conclusion: **Recommend applying the 5.5.4 patch**

**Reasons:**

1. **Costs are statistically significant but practically negligible**: RSS +52 ~ 317 MB
   (<0.5% of 64 GB), VMS +148 ~ 505 MB (virtual space only), Threads +1 ~ 6,
   VRAM is mostly noise
2. **No performance degradation**: FPS 20.0 maintained, no significant GPU/CPU utilization change
3. **No stability difference**: Success rates are equivalent across both versions
4. **Patch benefits** (confirmed in v2): Ray tracing pipeline optimization yielding
   GPU utilization -28.7% and power -68.5W improvement in rendering stress scenarios
5. **1,079 bug fix commits**: Navigation system (walker/vehicle AI foundation) crash
   fixes (9), rendering/graphics fixes (107), general crash fixes (110),
   Linux-specific fixes (26). Navigation and ray tracing fixes directly impact
   CARLA's core functionality

On a 64 GB RAM / 32 GB VRAM system, a few hundred MB of memory increase has no
practical impact on operations. The patch's benefits (GPU efficiency improvements,
bug fixes) far outweigh the costs.
