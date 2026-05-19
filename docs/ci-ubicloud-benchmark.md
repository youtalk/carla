# Ubicloud vCPU Cost-Performance Benchmark

Date: 2026-05-19
Repository: `youtalk/carla`
Workflow: `.github/workflows/ubicloud_benchmark.yml`
Spec: `docs/superpowers/specs/2026-05-19-ubicloud-vcpu-benchmark-design.md`

## Setup

- Workload: LibCarla (`carla-client`, `carla-server`) + `carla-python-api`, clean build.
- Container: `ghcr.io/youtalk/carla-ue5-toolchain:24.04`.
- Runners: Ubicloud x64 standard, 8 / 16 / 30 vCPU.
- Repetitions: 1 run per size.
- Billing: $0.0004 per vCPU-minute (linear, per-minute).
- Source run: GitHub Actions run 26114644965.

## Results

| runner | build-phase | billed job time | billed min | cost/run | speedup vs 8 | parallel eff. | cost vs 8 |
|:-------|------------:|----------------:|-----------:|---------:|-------------:|--------------:|----------:|
| ubicloud-standard-8 | 4m00s | 6m27s | 7 | $0.0224 | 1.00x | 1.00 | 1.00x |
| ubicloud-standard-16 | 2m38s | 5m06s | 6 | $0.0384 | 1.26x | 0.63 | 1.71x |
| ubicloud-standard-30 | 2m17s | 4m09s | 5 | $0.0600 | 1.55x | 0.41 | 2.68x |

`build-phase` is the configure + 3 build targets only; `billed job time` is the
whole job (container pull, checkout, conda init, build, artifact upload), which
is what Ubicloud meters.

## Analysis

- Cost-optimal size: **ubicloud-standard-8** at $0.0224/run.
- Recommended size (Balanced rule): **ubicloud-standard-8**.

The compiler workload itself parallelizes acceptably — build-phase time drops
from 4m00s to 2m17s — but with clearly diminishing returns: parallel
efficiency falls from 1.00 (8 vCPU) to 0.63 (16) to 0.41 (30). On top of that,
each job carries roughly 2.5 minutes of fixed, non-parallelizable overhead
(container image pull, checkout, conda init, artifact upload). That overhead
dilutes the wall-clock gain: measured on billed job time, 16 vCPU is only 1.26x
faster and 30 vCPU only 1.55x faster than 8 vCPU.

Because Ubicloud bills linearly per vCPU-minute, cost rises far faster than
speed: 16 vCPU costs 1.71x and 30 vCPU 2.68x the 8-vCPU run. The Balanced rule
keeps 8 vCPU — 30 vCPU does clear the "at least 30% faster" bar (249s is within
0.70 x 387s) but costs 2.68x the cost-optimal, far above the 1.15x premium the
rule allows; 16 vCPU clears neither bar.

## Decision

The production reusable workflow `_ci-ubicloud.yml` default `vcpu` is set to
**8**. `ubicloud_dev.yml` and `ubicloud_pr.yml` inherit this default, so push
and PR CI now run on `ubicloud-standard-8` — about 2.5 minutes slower per run
than the previous `ubicloud-standard-30`, in exchange for a 2.68x lower cost
per run.

## Caveats

- 1 run per size: no variance data. A single slow run could shift the ranking.
  Re-run the `ubicloud_benchmark.yml` workflow to collect more samples.
- Measures the LibCarla + PythonAPI build only (`BUILD_CARLA_UNREAL=OFF`),
  matching the current Ubicloud CI scope.
- The ~2.5 min fixed overhead is a large share of the 8-vCPU run; reducing it
  (e.g. a slimmer container image, dependency caching) would shift the
  cost-performance balance and is worth a future look.
