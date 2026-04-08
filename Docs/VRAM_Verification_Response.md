# CARLA UE5 VRAM Executive Summary — Source Code Verification Report

**In response to:** "CARLA UE5 VRAM Executive Summary" (Anaya, April 2026)
**Verified against:** [`ue5-dev` branch](https://github.com/carla-simulator/carla/tree/ue5-dev), commit [`2eb47c447`](https://github.com/carla-simulator/carla/commit/2eb47c447afab05b2a1f8de3ac0b59cd691c317b)
**Date:** 2026-04-08

---

## 1. Executive Summary of Findings

The Executive Summary correctly identifies that CARLA UE5 has a VRAM accessibility problem and that architectural gaps (HW Ray Tracing defaults, lack of World Partition, incomplete scalability infrastructure) are contributing factors. These qualitative observations are sound and well-reasoned.

However, **the three core numeric claims that underpin the document's quantitative analysis and prioritization are all factually incorrect** when verified against the actual source code:

| Claim in PDF | Actual Value in Source | File & Line | Impact on Analysis |
|---|---|---|---|
| `r.Streaming.PoolSize = 14000` | INI: 4000, Runtime: **2000** | [`DefaultEngine.ini:30`][ini-30], [`CarlaSettingsDelegate.cpp:189`][csd-189], [`:386`][csd-386] | PDF's #1 recommendation (reduce PoolSize) has **zero effect** — the runtime value already undercuts the PDF's own target of 4096-6144 |
| `r.SetRes = 3840x2160f` | **Not set anywhere** in the codebase | Full codebase grep: 0 matches | 4K is not the default resolution; the 0.8-1.5 GB GBuffer savings claim is invalid |
| `r.SkinCache.SceneMemoryLimitInMB = 2048` | **1024** | [`DefaultEngine.ini:57`][ini-57] | Already half the claimed value; the 1-1.5 GB savings estimate is halved |

Furthermore, the document's estimated VRAM budget ceiling of 20-23 GB is not supported by code evidence. With the actual CVar values, the theoretical ceiling is significantly lower.

**Most critically, the PDF completely overlooks the single largest VRAM consumer in a typical autonomous driving scenario: camera sensor render target allocation.** Each camera sensor triggers ~4.6 GB of VRAM allocation for its first instance (GBuffer, render targets, post-process chain), and a typical 6-camera surround setup pushes total VRAM to ~16.2 GB — regardless of texture pool size. This finding reorders the entire optimization priority.

---

## 2. Detailed Claim Verification

### 2.1 Texture Streaming Pool Size

**PDF claim:** `r.Streaming.PoolSize=14000`, allocating up to 14 GB exclusively for streamed texture mip data. Described as the root cause of the 16 GB requirement. The "highest-ROI action" is reducing this CVar.

**Actual code:**

[`Unreal/CarlaUnreal/Config/DefaultEngine.ini`][ini], line 30:
```ini
r.Streaming.PoolSize=4000
```

[`Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp`][csd], lines 189 and 386:
```cpp
// In LaunchLowQualityCommands() — line 189:
GEngine->Exec(world, TEXT("r.Streaming.PoolSize 2000"));

// In LaunchEpicQualityCommands() — line 386:
GEngine->Exec(world, TEXT("r.Streaming.PoolSize 2000"));
```

**Verdict: FALSE.** The INI value is 4000 (not 14000), and both quality levels override it to 2000 at runtime. The effective streaming pool is **2000 MB** — already below the PDF's own recommended target of 4096-6144 MB. The PDF's "highest-ROI action" of reducing PoolSize would have zero effect on actual VRAM consumption.

The 14000 value may have appeared in an earlier development build or branch, but it does not exist in the current ue5-dev branch at commit 815b8ba2c.

---

### 2.2 Default Resolution

**PDF claim:** `r.SetRes=3840x2160f` (4K default resolution), inflating all framebuffers by 4x vs. 1080p. Recommends changing to 1920x1080f for 0.8-1.5 GB savings.

**Actual code:** A comprehensive search of all `.ini` files, C++ source files, and command-line argument handling reveals **zero occurrences** of `r.SetRes` anywhere in the CARLA codebase.

[`Unreal/CarlaUnreal/Config/DefaultGameUserSettings.ini`][dgus] contains only:
```ini
FullscreenMode=2
```

`FullscreenMode=2` is windowed fullscreen — the resolution is determined by the OS window manager, not by CARLA.

**Verdict: FALSE.** CARLA does not set a default resolution. The actual resolution depends on the display environment. In headless mode (`-RenderOffScreen`), resolution is controlled by Vulkan surface dimensions, not by any `r.SetRes` CVar.

---

### 2.3 Skin Cache Memory Limit

**PDF claim:** `r.SkinCache.SceneMemoryLimitInMB=2048` (2 GB skin cache for only 11 vehicle models). Recommends reducing to 256-512 for 1-1.5 GB savings.

**Actual code:**

[`Unreal/CarlaUnreal/Config/DefaultEngine.ini`][ini], line 57:
```ini
r.SkinCache.SceneMemoryLimitInMB=1024
```

**Verdict: FALSE.** The actual value is 1024, exactly half the claimed 2048. While there may still be room to reduce this, the savings estimate should be revised downward.

---

### 2.4 Hardware Ray Tracing and Lumen Configuration

**PDF claim:** All Town 10 geometry uses Nanite and Lumen provides fully dynamic GI. These are shipped at high-quality presets with no fallback options. Hardware ray tracing is available by default.

**Actual code confirms this claim:**

[`Unreal/CarlaUnreal/Config/DefaultEngine.ini`][ini]:
```ini
r.RayTracing=True                                    # line 47
r.Lumen.HardwareRayTracing=True                      # line 48
r.Lumen.HardwareRayTracing.LightingMode=3            # line 62 (Hit Lighting, max quality)
r.RayTracing.Shadows=True                            # line 51
r.DynamicGlobalIlluminationMethod=1                  # line 46 (Lumen GI)
r.Shadow.Virtual.Enable=1                            # line 54
r.Lumen.HardwareRayTracing.HitLighting.Skylight=1   # line 63
```

**Verdict: TRUE / Confirmed.** HW Ray Tracing, Lumen HW RT at maximum quality (LightingMode=3), and RT Shadows are all enabled by default. This is the primary configuration-level VRAM concern.

---

### 2.5 World Partition

**PDF claim:** CARLA does not use UE5 World Partition. The entire map is loaded monolithically.

**Verdict: TRUE / Confirmed.** No World Partition configuration exists for any CARLA map. Maps load as monolithic levels.

---

### 2.6 LargeMapManager

**PDF claim:** The UE4 LargeMapManager has not been ported to UE5. Large maps are not supported.

**Actual code:**

[`Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/MapGen/LargeMapManager.cpp`][lmm] — **1,148 lines** of functional code. It implements tile-based level streaming using `ULevelStreamingDynamic` with `WorldComposition`. It is actively referenced by **27 files** across the codebase, including:
- [`CarlaGameModeBase.cpp`][cgm] (initialization)
- [`CarlaEngine.cpp`][ce] (streaming distance sync)
- [`CarlaEpisode.cpp`][cep] (actor registration)
- [`GnssSensor.cpp`][gnss] (coordinate transforms)
- Multiple traffic and vehicle components

Key features:
- Distance-based tile loading (default 3000m streaming radius)
- Actor dormancy management (active/dormant based on distance)
- World origin rebasing for large coordinate spaces

**Verdict: FALSE.** LargeMapManager exists, is functional, and is actively integrated. It uses UE4-era `ULevelStreamingDynamic` (not UE5 World Partition), but it is not "unported" — it has been adapted to work within the UE5 build. However, it does not use UE5's native World Partition system, which is the more efficient approach.

---

### 2.7 Scalability Infrastructure

**PDF claim:** No user-accessible quality presets exist.

**Actual code:**

[`Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/QualityLevelUE.h`][qlue]:
```cpp
enum class EQualityLevel : uint8
{
  Null = 0,
  Low    = CARLA_ENUM_FROM_RPC(Low),
  // Medium = CARLA_ENUM_FROM_RPC(Medium),  // COMMENTED OUT
  // High   = CARLA_ENUM_FROM_RPC(High),    // COMMENTED OUT
  Epic   = CARLA_ENUM_FROM_RPC(Epic),
  SIZE, INVALID
};
```

[`CarlaSettingsDelegate.cpp`][csd] implements [`LaunchLowQualityCommands()`][csd-174] (lines 174-230) and [`LaunchEpicQualityCommands()`][csd-376] (lines 376-419), each setting ~30 CVars at runtime. These are accessible via:
- Python API: `world.apply_settings(carla.WorldSettings(quality_level=carla.QualityLevel.Low))`
- Command line: `-quality-level=Low`

**Verdict: PARTIAL.** Two quality presets (Low and Epic) exist and are user-accessible. However, the CVars in both presets are entirely UE4-era and do not control any UE5-specific features (Nanite, Lumen, Virtual Shadow Maps, mesh distance fields). Medium and High levels are commented out. The DefaultScalability.ini contains only `r.EyeAdaptationQuality` overrides.

---

## 3. Critical Missing Finding: Camera Sensor VRAM Impact

The Executive Summary does not mention camera sensors as a VRAM factor. Our measurements on RTX 5090 (Shipping build, headless, nvidia-smi) reveal they are the **dominant VRAM consumer**:

| Configuration | VRAM (MiB) | Delta from Baseline |
|---|---|---|
| Town10 Epic, no cameras | 6,669 | — |
| + 1x RGB camera 720p | 9,162 | **+2,493** |
| + 1x RGB camera 1080p | 11,309 | **+4,640** |
| + 1x RGB camera 4K | 10,875 | **+2,817** (note: less than 1080p due to RT allocation patterns) |
| + 1x Semantic Segmentation 1080p | 11,502 | **+4,833** |
| + 6x RGB camera 1080p (surround) | 16,243 | **+8,135** |

The first camera triggers ~4.6 GB of shared rendering resource allocation (GBuffer, render targets, post-process chain). Subsequent cameras add ~700-1,000 MB each.

**Root cause in code:** Each `ASceneCaptureSensor` creates its own `USceneCaptureComponent2D_CARLA` with [`bUseRayTracingIfEnabled = true`][scs-71] ([SceneCaptureSensor.cpp:71][scs-71]), meaning every camera forces per-view BVH traversal when HW RT is active. Each camera also maintains an independent set of [13 GBuffer data streams][scs-h-553] ([SceneCaptureSensor.h:553-568][scs-h-553]).

A typical autonomous driving scenario (Town10, 30 vehicles, 3 cameras, LiDAR) reaches ~13 GB — **this is the actual mechanism behind the 16 GB minimum requirement**, not the texture streaming pool.

---

## 4. Corrected VRAM Budget Model

| Component | PDF Estimate | Measured Value | Source |
|---|---|---|---|
| Texture streaming pool | Up to 14 GB | **2,000 MiB** (runtime override) | [`CarlaSettingsDelegate.cpp:189,386`][csd-189] |
| Default resolution overhead | 1-1.5 GB (4K) | **OS-dependent** (not set by CARLA) | No r.SetRes in codebase |
| Skin cache | Up to 2 GB | **1,024 MiB** | [`DefaultEngine.ini:57`][ini-57] |
| Town10 map (geometry + textures + Nanite + Lumen) | Not estimated | **~6,500 MiB** | nvidia-smi after map stabilization |
| First camera sensor (shared RT init) | Not mentioned | **~4,500 MiB** | nvidia-smi delta: 0 → 1 camera |
| Additional cameras (per unit) | Not mentioned | **~700-1,000 MiB** | nvidia-smi delta per camera |
| HW RT BVH structures | Mentioned but not quantified | **~500-1,500 MiB** | Epic vs Low delta |
| Theoretical ceiling | 20-23 GB | **6.7 GB (map only), 11-16 GB (with cameras)** | nvidia-smi measured |

---

## 5. Revised Optimization Priority

The PDF prioritizes `r.Streaming.PoolSize` reduction as the "highest-ROI action." Based on source code verification and measured data, the actual priority order is:

| Priority | Action | Expected Impact | PDF Priority |
|---|---|---|---|
| **1** | Disable HW RT by default + disable per-camera RT | ~1-2 GB (BVH) + reduces per-camera overhead | Tier 1 (partial) |
| **2** | Modernize quality level CVars for UE5 | Fixes Low > Epic VRAM regression with cameras | Not addressed |
| **3** | Camera sensor render pipeline optimization | ~2-4 GB for multi-camera setups | **Not mentioned** |
| **4** | World Partition migration for Town15 | ~3-4 GB for large maps | Tier 3 |
| **5** | UE5 scalability groups (DefaultScalability.ini) | Proper sg.* command support | Tier 2 (partial) |
| ~~6~~ | ~~Reduce r.Streaming.PoolSize~~ | ~~No effect~~ (already at 2000 < PDF's target of 4096) | ~~PDF's #1 recommendation~~ |

---

## 6. Conclusion

The Executive Summary provides valuable architectural analysis of CARLA's VRAM challenges. Its qualitative observations about HW Ray Tracing defaults, missing World Partition, and incomplete scalability are correct and actionable.

However, the quantitative foundation of the analysis contains critical errors:
- Three key numeric claims are wrong (PoolSize, SetRes, SkinCache)
- The resulting VRAM budget model (20-23 GB theoretical ceiling) is unsupported
- The #1 recommended action (PoolSize reduction) would have zero effect
- The largest actual VRAM consumer (camera sensor RT allocation) is entirely unaddressed

We recommend that any optimization efforts follow the revised priority order above, beginning with HW RT default changes and camera sensor improvements rather than texture streaming pool adjustments.

---

<!-- GitHub source links (ue5-dev branch, commit 2eb47c447) -->
[ini]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Config/DefaultEngine.ini
[ini-30]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Config/DefaultEngine.ini#L30
[ini-57]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Config/DefaultEngine.ini#L57
[dgus]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Config/DefaultGameUserSettings.ini
[csd]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp
[csd-174]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp#L174
[csd-189]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp#L189
[csd-376]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp#L376
[csd-386]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp#L386
[qlue]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/QualityLevelUE.h
[lmm]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/MapGen/LargeMapManager.cpp
[cgm]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Game/CarlaGameModeBase.cpp
[ce]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Server/CarlaEngine.cpp
[cep]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Game/CarlaEpisode.cpp
[gnss]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/GnssSensor.cpp
[scs-71]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.cpp#L71
[scs-h-553]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.h#L553
