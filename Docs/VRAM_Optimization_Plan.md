# CARLA UE5 VRAM Optimization Plan

**Target:** Enable CARLA 0.10.x to run on 8-12 GB GPUs for typical autonomous driving scenarios
**Branch:** [`ue5-dev`](https://github.com/carla-simulator/carla/tree/ue5-dev) (verified at commit [`2eb47c447`](https://github.com/carla-simulator/carla/commit/2eb47c447afab05b2a1f8de3ac0b59cd691c317b))
**Date:** 2026-04-08

---

## Current State

Measured on RTX 5090 (32 GB), Shipping build, headless (`-RenderOffScreen`), nvidia-smi polling:

| Scenario | VRAM |
|---|---|
| Town10 Epic, no cameras | 6.5 GB |
| Town10 Epic + 1x 1080p camera | 11.3 GB |
| Town10 Epic + 6x 1080p cameras | 16.2 GB |
| Town15 Epic, no cameras | 9.8 GB (peak during load: 15.2 GB) |
| Town10 Low + 6x 1080p cameras | **17.8 GB** (worse than Epic — bug) |

**Root causes identified (in order of impact):**

1. **Camera sensor render targets** — Each camera allocates ~4.6 GB of shared rendering infrastructure on first spawn (GBuffer, render targets, post-process chain). 6-camera surround adds ~8.1 GB total. Each camera also forces per-view BVH traversal ([`bUseRayTracingIfEnabled=true`][scs-71]).

2. **HW Ray Tracing enabled by default** — [`r.Lumen.HardwareRayTracing=True`][ini-48] and [`r.RayTracing.Shadows=True`][ini-51] force BVH acceleration structure construction, adding ~1-2 GB.

3. **Monolithic map loading** — Town15 loads all 480 actors into GPU memory simultaneously. No World Partition. [`LargeMapManager`][lmm] exists (1,148 lines) but uses UE4-era `ULevelStreamingDynamic`, not UE5 native World Partition.

4. **UE4-era quality presets** — Low and Epic quality commands ([`CarlaSettingsDelegate.cpp`][csd]) set ~30 CVars from UE4 but none for UE5 features (Nanite, Lumen, Virtual Shadow Maps). Low mode with 6 cameras uses MORE VRAM than Epic.

5. **No UE5 scalability groups** — [`DefaultScalability.ini`][dsi] contains only `r.EyeAdaptationQuality`. No entries for shadows, GI, reflections, Nanite, or texture streaming quality groups.

---

## Phase 1: Configuration and Code Quick Fixes (1-2 weeks)

Expected outcome: Town10 + 3 cameras fits in 10-12 GB. Low mode regression fixed.

### 1.1 Disable Lumen HW RT and RT Shadows by Default

**Files:** [`Unreal/CarlaUnreal/Config/DefaultEngine.ini`][ini]

Change the following defaults. Keep [`r.RayTracing=True`][ini-47] (controls shader compilation; required for opt-in):

```diff
- r.Lumen.HardwareRayTracing=True
+ r.Lumen.HardwareRayTracing=False

- r.RayTracing.Shadows=True
+ r.RayTracing.Shadows=False

- r.Lumen.HardwareRayTracing.LightingMode=3
- r.Lumen.HardwareRayTracing.HitLighting.Skylight=1
+ ; HW RT Lumen settings (enable with r.Lumen.HardwareRayTracing=True for 16+ GB GPUs)
+ ; r.Lumen.HardwareRayTracing.LightingMode=3
+ ; r.Lumen.HardwareRayTracing.HitLighting.Skylight=1
```

Lumen continues to work via Software Ray Tracing (SDF-based). Users opt-in to HW RT via:
```
-dpcvars=r.Lumen.HardwareRayTracing=1,r.RayTracing.Shadows=1
```

**Expected savings:** ~1-2 GB (BVH elimination).

### 1.2 Disable Per-Camera Ray Tracing

**Files:** [`Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.cpp`][scs]

```diff
- CaptureComponent2D->bUseRayTracingIfEnabled = true;   // line 71
+ CaptureComponent2D->bUseRayTracingIfEnabled = false;
```

Prevents each `USceneCaptureComponent2D` from forcing per-view BVH traversal. Cameras render correctly via rasterization + Lumen SW RT.

**Expected savings:** Reduces per-camera VRAM overhead (exact amount depends on BVH per-view cost).

### 1.3 Add UE5 CVars to Low Quality Level

**Files:** [`Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp`][csd]

Append to [`LaunchLowQualityCommands()`][csd-174] (after [line 228][csd-228]):

```cpp
// UE5-specific: aggressive VRAM reduction for 8 GB target
GEngine->Exec(world, TEXT("r.Lumen.HardwareRayTracing 0"));
GEngine->Exec(world, TEXT("r.RayTracing.Shadows 0"));
GEngine->Exec(world, TEXT("r.DynamicGlobalIlluminationMethod 0"));
GEngine->Exec(world, TEXT("r.ReflectionMethod 0"));
GEngine->Exec(world, TEXT("r.Shadow.Virtual.MaxPhysicalPages 512"));
GEngine->Exec(world, TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional 1"));
GEngine->Exec(world, TEXT("r.Nanite.MaxPixelsPerEdge 2"));
GEngine->Exec(world, TEXT("r.Nanite.Streaming.StreamingPoolSize 256"));
GEngine->Exec(world, TEXT("r.Streaming.PoolSize 1500"));
GEngine->Exec(world, TEXT("r.Lumen.TraceMeshSDFs 0"));
GEngine->Exec(world, TEXT("r.GenerateMeshDistanceFields 0"));
```

**Expected outcome:** Low mode properly reduces VRAM; fixes the Low > Epic regression.

### 1.4 Add UE5 CVars to Epic Quality Level

**Files:** [`Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp`][csd]

Append to [`LaunchEpicQualityCommands()`][csd-376] (after [line 418][csd-418]):

```cpp
// UE5-specific: Lumen SW RT with proper budgets for 16+ GB target
GEngine->Exec(world, TEXT("r.Lumen.HardwareRayTracing 0"));
GEngine->Exec(world, TEXT("r.RayTracing.Shadows 0"));
GEngine->Exec(world, TEXT("r.DynamicGlobalIlluminationMethod 1"));
GEngine->Exec(world, TEXT("r.ReflectionMethod 1"));
GEngine->Exec(world, TEXT("r.Shadow.Virtual.MaxPhysicalPages 4096"));
GEngine->Exec(world, TEXT("r.Nanite.MaxPixelsPerEdge 1"));
GEngine->Exec(world, TEXT("r.Nanite.Streaming.StreamingPoolSize 1024"));
GEngine->Exec(world, TEXT("r.Streaming.PoolSize 2000"));
GEngine->Exec(world, TEXT("r.Lumen.TraceMeshSDFs 1"));
GEngine->Exec(world, TEXT("r.GenerateMeshDistanceFields 1"));
```

### 1.5 Add UE5 Scalability Groups

**Files:** [`Unreal/CarlaUnreal/Config/DefaultScalability.ini`][dsi]

Add quality group overrides for Shadow (VSM pages), GlobalIllumination (Lumen method), Reflection, ViewDistance (Nanite quality/pool), and Texture (streaming pool) across @0 (Low) through @3 (Epic) tiers. This ensures `sg.*` console commands and UE5's built-in scalability menu properly control VRAM-impacting features.

---

## Phase 2: Camera Sensor Optimization (2-4 weeks)

Expected outcome: First camera overhead reduced from ~4.6 GB to ~2-3 GB. 6-camera setup fits in 12 GB.

### 2.1 Investigate Shared Rendering Resource Pool

The ~4.6 GB spike for the first camera is caused by UE5's `SceneCaptureComponent2D` allocating a full rendering pipeline (GBuffer, post-process chain, Lumen scene structures). Investigate whether:

- A shared render target pool can be used across cameras
- GBuffer allocation can be deferred until first capture (currently allocated at [`BeginPlay`][scs-874])
- Post-processing can be conditionally disabled per sensor type

**Files:** [`SceneCaptureSensor.cpp`][scs], [`SceneCaptureSensor.h`][scs-h], [`SceneCaptureComponent2D_CARLA`][scc2d]

### 2.2 Disable Post-Processing for Non-Visual Sensors

Depth, semantic segmentation, and instance segmentation cameras do not need the full post-process chain. Disabling it per-sensor should reduce per-camera VRAM:

- Set `bEnablePostProcessingEffects = false` for non-RGB sensors
- Use simplified ShowFlags (disable Lumen, bloom, DOF, motion blur) per-sensor
- Consider using `ESceneCaptureSource::SCS_SceneColorHDR` instead of `SCS_FinalToneCurveHDR` for non-visual sensors

### 2.3 Camera Render Target Resolution Scaling by Quality Level

Add quality-level-aware resolution scaling for camera sensors:
- Low quality: cap at 720p equivalent, use TSR upscaling
- Epic quality: native resolution (current behavior)

This requires modifying [`SceneCaptureSensor::BeginPlay()`][scs-874] to query the current quality level and adjust [`ImageWidth`/`ImageHeight`][scs-h-601] before [`CaptureRenderTarget->InitCustomFormat()`][scs-881].

### 2.4 Re-enable Medium Quality Level

Restore the `Medium` quality level (currently commented out in [`QualityLevelUE.h`][qlue] and [`rpc/QualityLevel.h`][rpc-ql]) with UE5-appropriate settings targeting 12 GB VRAM.

**Consideration:** Uncommenting `Medium` changes the RPC enum integer layout — `Epic` shifts from value 1 to 3. This requires a coordinated Python API update and should be released as a minor version bump to avoid breaking existing scripts.

---

## Phase 3: World Partition Migration (2-4 months)

Expected outcome: Town15 fits in 8-10 GB. Maps only load 30-40% of assets at any time.

### 3.1 Background

UE5's World Partition divides large maps into grid cells and streams only those near the camera. CARLA does not use it. The current [`LargeMapManager`][lmm] (1,148 lines, [27 file references][lmm-refs]) provides tile streaming via `ULevelStreamingDynamic` and `WorldComposition`, but this is a UE4-era approach that:

- Does not integrate with UE5's native HLOD system
- Does not benefit from UE5's optimized streaming scheduler
- Cannot generate proxy meshes for distant cells
- Requires manual tile management rather than automatic grid partitioning

Town15 (480 actors) loads monolithically and consumes 9.8 GB baseline, with a load-time peak of 15.2 GB. World Partition could reduce resident assets to 30-40% of total, potentially saving 3-4 GB in steady state and eliminating the load spike.

### 3.2 Migration Strategy

#### Step 1: Town10HD_Opt Pilot Conversion (4-6 weeks)

Start with Town10HD_Opt as it is the default map and smallest. This is primarily UE5 Editor work:

1. **Enable World Partition** on the Town10HD_Opt level in UE5 Editor
2. **Set grid cell size** to 128m-256m (balance between streaming granularity and overhead)
3. **Configure streaming source** to follow the ego vehicle actor
4. **Validate sensor output** — ensure all camera types produce correct results with streamed levels
5. **Validate LargeMapManager compatibility** — determine if LargeMapManager and World Partition can coexist or if LargeMapManager needs refactoring

Key concerns:
- CARLA sensors use custom stencil values for semantic segmentation. These must be preserved across streaming boundaries.
- Traffic Manager assumes all actors are resident. Dormant actors in unloaded cells need proper handling.
- OpenDRIVE road network is loaded as a single graph. Streaming must not fragment the route planner.

#### Step 2: HLOD Generation (2-3 weeks)

Generate Hierarchical LODs for distant World Partition cells:

1. **Configure HLOD layers** in UE5 Editor:
   - Layer 0 (close): Full Nanite meshes (current behavior)
   - Layer 1 (medium): Simplified proxy meshes with merged materials
   - Layer 2 (far): Imposter/billboard representations
2. **Build HLOD proxies** using UE5's built-in HLOD builder
3. **Validate VRAM** — measure steady-state VRAM with HLOD vs. monolithic loading

#### Step 3: Town15 Conversion (3-4 weeks)

Apply the validated workflow from Town10 to Town15:

1. Convert Town15 to World Partition (128m cells)
2. Generate HLOD layers
3. Adjust streaming radius to balance visual quality vs. VRAM budget
4. Validate with typical scenarios (ego vehicle driving through the full map)

#### Step 4: LargeMapManager Modernization (2-3 weeks)

Evaluate and modernize LargeMapManager:

1. **Option A: Adapt to World Partition** — Replace `ULevelStreamingDynamic` + `WorldComposition` with UE5 World Partition APIs and Data Layers. Keep the actor dormancy management.
2. **Option B: Deprecate in favor of native WP** — If UE5 World Partition handles all CARLA's needs (streaming, actor management, coordinate rebasing), LargeMapManager can be reduced to a thin compatibility layer.

The choice depends on how well UE5 World Partition handles CARLA-specific requirements (traffic simulation, sensor actor management, OpenDRIVE integration).

### 3.3 World Partition Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Semantic segmentation breaks across cell boundaries | Test stencil value persistence; add boundary overlap if needed |
| Traffic Manager actor-not-found for unloaded cells | Integrate TM with WP actor streaming callbacks |
| OpenDRIVE route planner fragmentation | Keep road network loaded as persistent sublevel |
| LargeMapManager conflicts with WP | Phase: pilot on Town10 first, then reconcile |
| Content asset repo needs restructuring | Coordinate with Bitbucket carla-content maintainers |
| Build pipeline changes | Update CI workflows to include WP cooking steps |

---

## Phase 4: Runtime VRAM Budget Manager (Optional, 1-2 months)

For truly robust 8 GB GPU support, implement a runtime VRAM monitoring and adaptation system:

### 4.1 VRAM Query and Adaptation

- Query available VRAM at startup via Vulkan/D3D12 API
- Auto-select quality level based on detected GPU memory:
  - < 8 GB: Low quality, cap camera count, warn user
  - 8-12 GB: Medium quality (once re-enabled)
  - 12-16 GB: Epic quality (SW RT)
  - 16+ GB: Epic quality (allow HW RT opt-in)
- Monitor VRAM pressure at runtime and dynamically adjust texture streaming pool and Nanite streaming pool

### 4.2 Expose via Python API

```python
# Proposed API additions
world_settings = world.get_settings()
world_settings.vram_budget_mb = 8192  # Target VRAM budget
world.apply_settings(world_settings)

# Query current VRAM usage
stats = world.get_rendering_stats()
print(f"VRAM used: {stats.vram_used_mb} / {stats.vram_total_mb} MB")
```

---

## Summary Timeline

| Phase | Scope | Effort | Target VRAM |
|---|---|---|---|
| **Phase 1** | Config + quality level fixes | 1-2 weeks | Town10 + 3 cameras: ~10-12 GB |
| **Phase 2** | Camera sensor optimization | 2-4 weeks | Town10 + 6 cameras: ~10-12 GB |
| **Phase 3** | World Partition migration | 2-4 months | Town15 + cameras: ~8-10 GB |
| **Phase 4** | Runtime VRAM budget (optional) | 1-2 months | Auto-adaptation to any GPU |

**Phase 1 alone makes CARLA usable on 12 GB GPUs (RTX 3060/4070 class) for most research scenarios.**

Phases 1 + 2 target **10-12 GB** for multi-camera setups.

Phases 1-3 target **8 GB GPU compatibility** for all maps including Town15.

---

## Verification Matrix

After each phase, validate with the following test matrix (nvidia-smi, headless Shipping build):

| Scenario | Phase 1 Target | Phase 1+2 Target | Phase 1+2+3 Target |
|---|---|---|---|
| Town10 Epic, no cameras | < 6 GB | < 5.5 GB | < 5 GB |
| Town10 Epic + 1x 1080p cam | < 9 GB | < 7 GB | < 7 GB |
| Town10 Epic + 6x 1080p cam | < 14 GB | < 12 GB | < 10 GB |
| Town10 Low + 6x 1080p cam | < Epic (regression fixed) | < 10 GB | < 8 GB |
| Town15 Epic, no cameras | ~9 GB (unchanged) | ~9 GB | < 6 GB |
| Town15 Epic + 3 cameras | > 12 GB (still problematic) | ~12 GB | < 8 GB |

## Related GitHub Issues

- [#8964](https://github.com/carla-simulator/carla/issues/8964) — CARLA UE5 out of memory (Vulkan OOM on <16 GB GPUs)
- [#7379](https://github.com/carla-simulator/carla/issues/7379) — UE5 Reimplement MapLayers / World Partition migration
- [#8395](https://github.com/carla-simulator/carla/issues/8395) — Performance Loss in UE5.5 (FlushRenderingCommands)
- [#8575](https://github.com/carla-simulator/carla/issues/8575) — GPU memory crashes on 8-12 GB cards
- [Discussion #8525](https://github.com/carla-simulator/carla/discussions/8525) — Vulkan memory error with CARLA 0.10.0

---

<!-- GitHub source links (ue5-dev branch, commit 2eb47c447) -->
[ini]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Config/DefaultEngine.ini
[ini-47]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Config/DefaultEngine.ini#L47
[ini-48]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Config/DefaultEngine.ini#L48
[ini-51]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Config/DefaultEngine.ini#L51
[dsi]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Config/DefaultScalability.ini
[csd]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp
[csd-174]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp#L174
[csd-228]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp#L228
[csd-376]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp#L376
[csd-418]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/CarlaSettingsDelegate.cpp#L418
[qlue]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Settings/QualityLevelUE.h
[rpc-ql]: https://github.com/carla-simulator/carla/blob/ue5-dev/LibCarla/source/carla/rpc/QualityLevel.h
[scs]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.cpp
[scs-71]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.cpp#L71
[scs-874]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.cpp#L874
[scs-881]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.cpp#L881
[scs-h]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.h
[scs-h-553]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.h#L553
[scs-h-601]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/SceneCaptureSensor.h#L601
[scc2d]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Sensor/UE4_Overridden/SceneCaptureComponent2D_CARLA.h
[lmm]: https://github.com/carla-simulator/carla/blob/ue5-dev/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/MapGen/LargeMapManager.cpp
[lmm-refs]: https://github.com/carla-simulator/carla/search?q=LargeMapManager&type=code
