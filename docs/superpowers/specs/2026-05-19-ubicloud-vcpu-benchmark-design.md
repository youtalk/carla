# Ubicloud vCPU Cost-Performance Benchmark — Design

Date: 2026-05-19
Branch: `ci/ubicloud-vcpu-benchmark` (based on `origin/ue5-dev`)
Repository: `youtalk/carla` (the `origin` remote)

## Goal

The `origin` remote already runs its CI on Ubicloud GitHub Actions runners
(`_ci-ubicloud.yml`, `ubicloud_dev.yml`, `ubicloud_pr.yml`), hardcoded to
`ubicloud-standard-30`. Determine which Ubicloud x64 standard runner size gives
the best cost-performance for the CARLA LibCarla + PythonAPI build, then apply
the winner to the production workflows.

## Decisions (locked)

| Topic | Decision |
|---|---|
| Runner sizes | x64 standard: **8 / 16 / 30 vCPU** (Ubicloud x64 has no 32; max is 30) |
| Architecture | x64 only — reuses the existing `carla-ue5-toolchain` x64 Docker image |
| Repetitions | **1 run per size** (3 jobs total) |
| Ubuntu version | **24.04 only** |
| Approach | **B** — parameterize `_ci-ubicloud.yml`, add a thin benchmark wrapper |
| End state | Benchmark + report + **apply the winner** to production workflows |
| Decision rule | **Balanced** — see "Picking the winner" below |

## Cost model

Ubicloud x64 standard runners bill linearly and per-minute at
**$0.0004 per vCPU-minute** ($0.0008/min at 2 vCPU … $0.0120/min at 30 vCPU).

```
cost_per_run = ceil(job_seconds / 60) × vcpu × 0.0004   (USD)
```

No Ubicloud billing API is needed: job duration from the GitHub Actions API is
the authoritative input. Because cost is linear in vCPU-minutes, the cheapest
size is whichever minimizes `vcpu × wall_time`; the benchmark locates the knee
of that curve.

## Workload

The existing CI build, unchanged:

1. `cmake --preset Release -DENABLE_ROS2=ON -DBUILD_CARLA_UNREAL=OFF -DBUILD_EXAMPLES=OFF -Dstandard_math_library_linked_to_as_m=TRUE`
2. Build target `carla-client`
3. Build target `carla-server`
4. Build target `carla-python-api`

This is a clean build with no compiler cache (the current workflow has none),
which keeps the vCPU-scaling comparison fair across sizes.

## Architecture

Three jobs in a manually triggered workflow, plus one aggregation job.

```
ubicloud_benchmark.yml  (workflow_dispatch)
├─ benchmark  [matrix: vcpu = 8, 16, 30]
│    └─ uses: _ci-ubicloud.yml  (ubuntu-version=24.04, vcpu=<matrix>)
│         → 3 parallel jobs, each uploads bench-<vcpu>.json
└─ report     (needs: benchmark, runs-on: ubicloud-standard-2)
     ├─ download bench-*.json artifacts        (build-phase time)
     ├─ gh api .../runs/<run_id>/jobs          (authoritative billed time)
     ├─ compute cost + metrics
     └─ write comparison table to $GITHUB_STEP_SUMMARY + upload report
```

### Components

**`_ci-ubicloud.yml` (modified, reusable workflow)**

- New input `vcpu` (type `string`, default `"30"` — preserves current behavior).
- `runs-on: ubicloud-standard-${{ inputs.vcpu }}` (was hardcoded `-30`).
- Build-summary "Runner:" line uses `inputs.vcpu` instead of the hardcoded text.
- Wraps the configure + 3 build steps with `date +%s` start/end markers and
  computes `build_seconds`.
- Uploads a one-line JSON artifact `bench-<vcpu>.json`:
  `{"vcpu": <n>, "ubuntu": "24.04", "build_seconds": <n>}`.
- Build steps are otherwise unchanged.

**`ubicloud_benchmark.yml` (new)**

- Trigger: `workflow_dispatch` only. Dispatchable directly on the feature
  branch (`gh workflow run ubicloud_benchmark.yml --ref ci/ubicloud-vcpu-benchmark`),
  so no merge is required to run it.
- Job `benchmark`: `strategy.matrix.vcpu: [8, 16, 30]`, `fail-fast: false`,
  `uses: ./.github/workflows/_ci-ubicloud.yml` with `ubuntu-version: "24.04"`
  and `vcpu: ${{ matrix.vcpu }}`.
- Job `report`: `needs: benchmark`, `runs-on: ubicloud-standard-2`,
  `permissions: { actions: read, contents: read }`. Steps:
  1. `actions/download-artifact` for the three `bench-*.json` files.
  2. `gh api repos/${{ github.repository }}/actions/runs/${{ github.run_id }}/jobs --paginate`
     to read each build job's `started_at` / `completed_at`.
  3. Compute `job_seconds`, `billed_minutes = ceil(job_seconds/60)`,
     `cost = billed_minutes × vcpu × 0.0004`, speedup, parallel efficiency.
  4. Render a markdown table into `$GITHUB_STEP_SUMMARY`; upload it as
     `benchmark-report.md`.

**`docs/ci-ubicloud-benchmark.md` (new)**

The human-readable results report: raw numbers, the comparison table, the
recommendation per the decision rule, and the explicit 1-run / no-variance
caveat.

## Report metrics

Per runner size, the `report` job emits:

| Field | Definition |
|---|---|
| build-phase time | `build_seconds` from the job's artifact (compute only) |
| billed job time | `completed_at − started_at` from the API (whole job) |
| billed minutes | `ceil(billed_job_time / 60)` |
| cost/run | `billed_minutes × vcpu × $0.0004` |
| speedup vs 8 | `time_8 / time_size` |
| parallel efficiency | `speedup / (vcpu / 8)` |
| cost vs 8 | `cost_size / cost_8` |

## Picking the winner

Decision rule — **Balanced**:

1. Default winner = the size with the **lowest cost per run**.
2. Override to a larger size **only if** it is **≥30 % faster** in wall time
   **and** costs **≤15 % more** than the cost-optimal size.

This favors a low CI bill but will trade a small cost premium for a meaningful
cut in developer feedback latency.

## Applying the winner

Set the winning size as the `vcpu` input default in `_ci-ubicloud.yml`.
`ubicloud_dev.yml` and `ubicloud_pr.yml` call the reusable workflow without a
`vcpu` argument, so they inherit the new default automatically — no change
needed to those two files.

## Verification

The benchmark is its own test. Success criteria when the workflow is
dispatched on the feature branch:

- All three `benchmark` matrix jobs (8, 16, 30) complete successfully.
- The `report` job produces a fully populated comparison table with non-zero
  costs and a recommendation.

No unit tests apply to YAML workflow definitions.

## Integration

Final integration is a pull request from `ci/ubicloud-vcpu-benchmark` into
`ue5-dev`, created manually using the repository PR template. The PR is not
auto-created.

## Known limitations

- **No variance data.** 1 run per size means a single slow run could shift the
  ranking. Accepted per the locked decision. Re-running for more samples later
  only requires widening the matrix — no design change — so no `repetitions`
  input is built now (YAGNI).
- The benchmark measures the LibCarla + PythonAPI build only; it does not cover
  the full Unreal package build (`BUILD_CARLA_UNREAL=OFF`), matching the
  current Ubicloud CI scope.

## Out of scope

- arm64 runners and a separate arm64 toolchain image.
- ccache / dependency caching (would mask vCPU scaling).
- Ubuntu 22.04 in the benchmark matrix.
- Changes to the self-hosted GPU CI (`_ci-ubuntu.yml`, `ue5_*.yml`).
