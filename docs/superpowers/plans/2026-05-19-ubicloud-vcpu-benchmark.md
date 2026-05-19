# Ubicloud vCPU Cost-Performance Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Benchmark Ubicloud x64 runners at 8/16/30 vCPU on the CARLA LibCarla + PythonAPI build, then set the winning size as the production CI default on the `youtalk/carla` (`origin`) repository.

**Architecture:** Parameterize the existing reusable workflow `_ci-ubicloud.yml` with a `vcpu` input. A new manually triggered `ubicloud_benchmark.yml` runs that reusable workflow as a 3-way matrix (8/16/30 vCPU, Ubuntu 24.04) and a `report` job that pulls job durations from the GitHub API and runs a standalone, unit-tested shell script to render a cost-performance table. After the run, the winning size is applied as the `vcpu` default.

**Tech Stack:** GitHub Actions (reusable workflows, `workflow_dispatch`, matrix), Bash, `jq`, `gh` CLI, Ubicloud GitHub Actions runners.

**Spec:** `docs/superpowers/specs/2026-05-19-ubicloud-vcpu-benchmark-design.md`

**Branch:** `ci/ubicloud-vcpu-benchmark` (already created, based on `origin/ue5-dev`).

---

## File Structure

| File | Responsibility |
|---|---|
| `.github/workflows/_ci-ubicloud.yml` | Modified: `vcpu` input, parameterized `runs-on`, build-phase timing, per-run artifact |
| `Util/Tools/ubicloud_benchmark_report.sh` | New: pure function — artifacts + jobs JSON → markdown cost-performance table + winner |
| `Util/Tools/test_ubicloud_benchmark_report.sh` | New: self-contained test for the report script (fixtures + assertions) |
| `.github/workflows/ubicloud_benchmark.yml` | New: `workflow_dispatch` matrix wrapper + `report` job |
| `docs/ci-ubicloud-benchmark.md` | New: human-readable results report |

---

## Task 1: Parameterize `_ci-ubicloud.yml` with a `vcpu` input

**Files:**
- Modify: `.github/workflows/_ci-ubicloud.yml`

- [ ] **Step 1: Replace the whole file with the parameterized version**

Overwrite `.github/workflows/_ci-ubicloud.yml` with exactly this content:

```yaml
name: Ubicloud CI

on:
  workflow_call:
    inputs:
      ubuntu-version:
        type: string
        required: false
        default: "22.04"
      vcpu:
        type: string
        required: false
        default: "30"

jobs:
  build:
    name: Build (Ubuntu ${{ inputs.ubuntu-version }}, ${{ inputs.vcpu }} vCPU)
    runs-on: ubicloud-standard-${{ inputs.vcpu }}
    container:
      image: ghcr.io/${{ github.repository_owner }}/carla-ue5-toolchain:${{ inputs.ubuntu-version }}
      credentials:
        username: ${{ github.actor }}
        password: ${{ secrets.GITHUB_TOKEN }}
    env:
      CARLA_UNREAL_ENGINE_PATH: /unreal-engine

    defaults:
      run:
        shell: bash -leo pipefail {0}

    steps:
      - name: Init conda
        run: |
          conda init bash
          echo "source activate carla310" >> ~/.bashrc
          cat <<'EOF' > ~/.bash_profile
          if [ -f ~/.bashrc ]; then source ~/.bashrc; fi
          EOF

      - name: Checkout
        uses: actions/checkout@v4

      - name: Activate conda environment
        run: conda activate carla310

      - name: Record build start
        run: echo "BUILD_START=$(date +%s)" >> "$GITHUB_ENV"

      - name: Configure
        run: |
          cmake --preset Release \
            -DENABLE_ROS2=ON \
            -DBUILD_CARLA_UNREAL=OFF \
            -DBUILD_EXAMPLES=OFF \
            -Dstandard_math_library_linked_to_as_m=TRUE

      - name: Build LibCarla Client
        run: cmake --build Build/Release --target carla-client

      - name: Build LibCarla Server
        run: cmake --build Build/Release --target carla-server

      - name: Build PythonAPI
        run: cmake --build Build/Release --target carla-python-api

      - name: Record benchmark result
        run: |
          BUILD_SECONDS=$(( $(date +%s) - BUILD_START ))
          echo "Build phase: ${BUILD_SECONDS}s"
          printf '{"vcpu": %s, "ubuntu": "%s", "build_seconds": %s}\n' \
            "${{ inputs.vcpu }}" "${{ inputs.ubuntu-version }}" "${BUILD_SECONDS}" \
            > bench.json
          cat bench.json

      - name: Upload benchmark result
        uses: actions/upload-artifact@v4
        with:
          name: bench-${{ inputs.vcpu }}
          path: bench.json

      - name: Build summary
        if: always()
        run: |
          echo "## Build Summary (Ubuntu ${{ inputs.ubuntu-version }})" >> $GITHUB_STEP_SUMMARY
          echo "- Runner: ubicloud-standard-${{ inputs.vcpu }}" >> $GITHUB_STEP_SUMMARY
          echo "- Container: carla-ue5-toolchain:${{ inputs.ubuntu-version }}" >> $GITHUB_STEP_SUMMARY
          echo "- Targets: carla-client, carla-server, carla-python-api" >> $GITHUB_STEP_SUMMARY
```

Changes vs the original: added the `vcpu` input (default `"30"`, preserving current behavior); `runs-on` now `ubicloud-standard-${{ inputs.vcpu }}`; job `name` includes the vCPU count; new `Record build start`, `Record benchmark result`, `Upload benchmark result` steps; the `Build summary` "Runner:" line is no longer hardcoded.

- [ ] **Step 2: Verify the YAML parses**

Run:
```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/_ci-ubicloud.yml')); print('OK')"
```
Expected: prints `OK` with no traceback.

- [ ] **Step 3: Confirm callers still work unchanged**

The callers `ubicloud_dev.yml` and `ubicloud_pr.yml` pass only `ubuntu-version`. Confirm `vcpu` has a default so they remain valid:
```bash
grep -A4 'workflow_call' .github/workflows/_ci-ubicloud.yml | grep -A2 vcpu
```
Expected: shows `vcpu:` with `default: "30"`.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/_ci-ubicloud.yml
git commit -s -m "ci(ubicloud): parameterize runner vCPU size in reusable workflow"
```

---

## Task 2: Create and test the cost-performance report script

This task is TDD: write the test, watch it fail, write the script, watch it pass.

**Files:**
- Create: `Util/Tools/ubicloud_benchmark_report.sh`
- Test: `Util/Tools/test_ubicloud_benchmark_report.sh`

- [ ] **Step 1: Write the failing test**

Create `Util/Tools/test_ubicloud_benchmark_report.sh` with exactly this content:

```bash
#!/usr/bin/env bash
# Test for ubicloud_benchmark_report.sh — uses fixtures, no network.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="${HERE}/ubicloud_benchmark_report.sh"
FAILED=0

assert_contains() {
  # $1 = haystack, $2 = needle, $3 = label
  if [[ "$1" != *"$2"* ]]; then
    echo "FAIL: $3"
    echo "  expected to contain: $2"
    echo "  actual output:"
    echo "$1" | sed 's/^/    /'
    FAILED=1
  else
    echo "PASS: $3"
  fi
}

make_fixture() {
  # $1 = dir, $2..$4 = build_seconds for 8/16/30
  local dir="$1"
  mkdir -p "$dir/artifacts/bench-8" "$dir/artifacts/bench-16" "$dir/artifacts/bench-30"
  printf '{"vcpu": 8, "ubuntu": "24.04", "build_seconds": %s}\n'  "$2" > "$dir/artifacts/bench-8/bench.json"
  printf '{"vcpu": 16, "ubuntu": "24.04", "build_seconds": %s}\n' "$3" > "$dir/artifacts/bench-16/bench.json"
  printf '{"vcpu": 30, "ubuntu": "24.04", "build_seconds": %s}\n' "$4" > "$dir/artifacts/bench-30/bench.json"
}

# jobs.json with given START/END pairs (UTC ISO8601) for 8/16/30.
make_jobs() {
  local file="$1"
  cat > "$file" <<EOF
{"jobs": [
  {"name": "vCPU 8 / Build (Ubuntu 24.04, 8 vCPU)",  "started_at": "$2", "completed_at": "$3"},
  {"name": "vCPU 16 / Build (Ubuntu 24.04, 16 vCPU)", "started_at": "$4", "completed_at": "$5"},
  {"name": "vCPU 30 / Build (Ubuntu 24.04, 30 vCPU)", "started_at": "$6", "completed_at": "$7"},
  {"name": "Cost-performance report", "started_at": "$8", "completed_at": "$9"}
]}
EOF
}

# --- Test 1: poor scaling -> cost-optimal 8, no override, winner 8 ---
T1="$(mktemp -d)"
make_fixture "$T1" 580 540 520
make_jobs "$T1/jobs.json" \
  "2026-05-19T10:00:00Z" "2026-05-19T10:10:00Z" \
  "2026-05-19T10:00:00Z" "2026-05-19T10:09:20Z" \
  "2026-05-19T10:00:00Z" "2026-05-19T10:09:00Z" \
  "2026-05-19T10:11:00Z" "2026-05-19T10:11:30Z"
OUT1="$(bash "$SCRIPT" "$T1/artifacts" "$T1/jobs.json")"
assert_contains "$OUT1" "ubicloud-standard-30" "T1: 30-vCPU row present"
# 30 vCPU: ceil(540/60)=9 min -> 9 * 30 * 0.0004 = 0.1080
assert_contains "$OUT1" "0.1080" "T1: 30-vCPU cost computed"
assert_contains "$OUT1" "Recommended (Balanced rule): ubicloud-standard-8" "T1: winner is 8"

# --- Test 2: good scaling -> override to 16, 30 too costly ---
T2="$(mktemp -d)"
make_fixture "$T2" 1140 540 360
make_jobs "$T2/jobs.json" \
  "2026-05-19T10:00:00Z" "2026-05-19T10:20:00Z" \
  "2026-05-19T10:00:00Z" "2026-05-19T10:10:00Z" \
  "2026-05-19T10:00:00Z" "2026-05-19T10:07:00Z" \
  "2026-05-19T10:21:00Z" "2026-05-19T10:21:30Z"
OUT2="$(bash "$SCRIPT" "$T2/artifacts" "$T2/jobs.json")"
# 8: ceil(1200/60)=20 -> 20*8*.0004=0.0640 ; 16: 10 -> 10*16*.0004=0.0640 ; 30: 7 -> 7*30*.0004=0.0840
assert_contains "$OUT2" "Recommended (Balanced rule): ubicloud-standard-16" "T2: winner is 16"

rm -rf "$T1" "$T2"
if [[ "$FAILED" -ne 0 ]]; then echo "TESTS FAILED"; exit 1; fi
echo "ALL TESTS PASSED"
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
bash Util/Tools/test_ubicloud_benchmark_report.sh
```
Expected: FAIL — `ubicloud_benchmark_report.sh` does not exist yet (bash reports the script cannot be found / non-zero exit).

- [ ] **Step 3: Write the report script**

Create `Util/Tools/ubicloud_benchmark_report.sh` with exactly this content:

```bash
#!/usr/bin/env bash
# Render a Ubicloud vCPU cost-performance table from benchmark artifacts.
#
# Usage: ubicloud_benchmark_report.sh <artifact-dir> <jobs-json>
#   <artifact-dir>  directory containing bench-8/, bench-16/, bench-30/ each
#                   with a bench.json {"vcpu","ubuntu","build_seconds"}.
#   <jobs-json>     a GitHub Actions ".../runs/<id>/jobs" API response.
#
# Cost model: Ubicloud x64 standard bills $0.0004 per vCPU-minute, per minute.
set -euo pipefail

ARTIFACT_DIR="${1:?usage: ubicloud_benchmark_report.sh <artifact-dir> <jobs-json>}"
JOBS_JSON="${2:?usage: ubicloud_benchmark_report.sh <artifact-dir> <jobs-json>}"

RATE="0.0004"
VCPUS=(8 16 30)

declare -A BUILD_S JOB_S BILLED COST

for v in "${VCPUS[@]}"; do
  BUILD_S[$v]="$(jq -r '.build_seconds' "${ARTIFACT_DIR}/bench-${v}/bench.json")"
done

for v in "${VCPUS[@]}"; do
  read -r start end < <(jq -r --arg v "$v" \
    '.jobs[] | select(.name | test("vCPU " + $v + " /")) | "\(.started_at) \(.completed_at)"' \
    "$JOBS_JSON")
  s="$(date -d "$start" +%s)"
  e="$(date -d "$end" +%s)"
  JOB_S[$v]=$(( e - s ))
  BILLED[$v]=$(( (JOB_S[$v] + 59) / 60 ))
  COST[$v]="$(awk -v m="${BILLED[$v]}" -v c="$v" -v r="$RATE" 'BEGIN{printf "%.4f", m*c*r}')"
done

base_time="${JOB_S[8]}"
base_cost="${COST[8]}"

echo "## Ubicloud vCPU Cost-Performance Benchmark"
echo ""
echo "Workload: LibCarla + PythonAPI, Ubuntu 24.04, clean build, 1 run per size."
echo "Billing: \$${RATE} per vCPU-minute (Ubicloud x64 standard, per-minute)."
echo ""
echo "| vCPU | build-phase | billed job time | billed min | cost/run | speedup vs 8 | parallel eff. | cost vs 8 |"
echo "|-----:|------------:|----------------:|-----------:|---------:|-------------:|--------------:|----------:|"
for v in "${VCPUS[@]}"; do
  bs="${BUILD_S[$v]}"
  js="${JOB_S[$v]}"
  speedup="$(awk -v b="$base_time" -v j="$js" 'BEGIN{printf "%.2f", b/j}')"
  eff="$(awk -v sp="$speedup" -v v="$v" 'BEGIN{printf "%.2f", sp/(v/8.0)}')"
  cratio="$(awk -v c="${COST[$v]}" -v b="$base_cost" 'BEGIN{printf "%.2f", c/b}')"
  printf "| %s | %dm%02ds | %dm%02ds | %s | \$%s | %sx | %s | %sx |\n" \
    "$v" $((bs/60)) $((bs%60)) $((js/60)) $((js%60)) \
    "${BILLED[$v]}" "${COST[$v]}" "$speedup" "$eff" "$cratio"
done
echo ""

# Decision rule (Balanced): cheapest cost/run by default; override to a larger
# size only if it is >=30% faster AND costs <=15% more than the cost-optimal.
cost_opt=8
for v in "${VCPUS[@]}"; do
  if awk -v a="${COST[$v]}" -v b="${COST[$cost_opt]}" 'BEGIN{exit !(a < b)}'; then
    cost_opt="$v"
  fi
done

winner="$cost_opt"
for v in "${VCPUS[@]}"; do
  (( v > cost_opt )) || continue
  if awk -v t="${JOB_S[$v]}"  -v o="${JOB_S[$cost_opt]}"  'BEGIN{exit !(t <= 0.70*o)}' && \
     awk -v c="${COST[$v]}"   -v o="${COST[$cost_opt]}"   'BEGIN{exit !(c <= 1.15*o)}'; then
    winner="$v"
  fi
done

echo "**Cost-optimal:** ubicloud-standard-${cost_opt} (\$${COST[$cost_opt]}/run)"
echo ""
echo "**Recommended (Balanced rule): ubicloud-standard-${winner}**"
```

- [ ] **Step 4: Make the script executable and run the test to verify it passes**

Run:
```bash
chmod +x Util/Tools/ubicloud_benchmark_report.sh Util/Tools/test_ubicloud_benchmark_report.sh
bash Util/Tools/test_ubicloud_benchmark_report.sh
```
Expected: every line `PASS: ...` then `ALL TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add Util/Tools/ubicloud_benchmark_report.sh Util/Tools/test_ubicloud_benchmark_report.sh
git commit -s -m "ci(ubicloud): add cost-performance report script with tests"
```

---

## Task 3: Create the benchmark workflow

**Files:**
- Create: `.github/workflows/ubicloud_benchmark.yml`

- [ ] **Step 1: Create the workflow file**

Create `.github/workflows/ubicloud_benchmark.yml` with exactly this content:

```yaml
name: Ubicloud-Benchmark

on:
  workflow_dispatch:

jobs:
  benchmark:
    name: vCPU ${{ matrix.vcpu }}
    strategy:
      fail-fast: false
      matrix:
        vcpu: ["8", "16", "30"]
    uses: ./.github/workflows/_ci-ubicloud.yml
    with:
      ubuntu-version: "24.04"
      vcpu: ${{ matrix.vcpu }}

  report:
    name: Cost-performance report
    needs: benchmark
    runs-on: ubicloud-standard-2
    permissions:
      actions: read
      contents: read
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Download benchmark artifacts
        uses: actions/download-artifact@v4
        with:
          pattern: bench-*
          path: artifacts

      - name: Fetch job durations
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          gh api "repos/${GITHUB_REPOSITORY}/actions/runs/${GITHUB_RUN_ID}/jobs?per_page=100" > jobs.json

      - name: Generate report
        run: |
          bash Util/Tools/ubicloud_benchmark_report.sh artifacts jobs.json \
            | tee report.md >> "$GITHUB_STEP_SUMMARY"

      - name: Upload report
        uses: actions/upload-artifact@v4
        with:
          name: benchmark-report
          path: report.md
```

- [ ] **Step 2: Verify the YAML parses**

Run:
```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ubicloud_benchmark.yml')); print('OK')"
```
Expected: prints `OK`.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ubicloud_benchmark.yml
git commit -s -m "ci(ubicloud): add vCPU cost-performance benchmark workflow"
```

---

## Task 4: Run the benchmark on `origin`

This task produces the measurement data. Its outputs feed Task 5.

- [ ] **Step 1: Push the branch to `origin`**

```bash
git push -u origin ci/ubicloud-vcpu-benchmark
```
Expected: branch created on `youtalk/carla`.

- [ ] **Step 2: Dispatch the benchmark workflow on this branch**

```bash
gh workflow run ubicloud_benchmark.yml --repo youtalk/carla --ref ci/ubicloud-vcpu-benchmark
```
Expected: `✓ Created workflow_dispatch event for ubicloud_benchmark.yml at ci/ubicloud-vcpu-benchmark`.

- [ ] **Step 3: Capture the run ID**

Wait a few seconds, then:
```bash
gh run list --repo youtalk/carla --workflow ubicloud_benchmark.yml \
  --branch ci/ubicloud-vcpu-benchmark --limit 1 --json databaseId,status,url
```
Record the `databaseId` as `RUN_ID`.

- [ ] **Step 4: Watch the run to completion**

```bash
gh run watch <RUN_ID> --repo youtalk/carla --exit-status
```
Expected: the `benchmark` matrix (8/16/30) and the `report` job all complete successfully. If a build job fails, stop and debug — do not proceed to Task 5 with partial data. (Common causes: the `carla-ue5-toolchain:24.04` GHCR image must exist and be readable; a build break.)

- [ ] **Step 5: Download the rendered report**

```bash
gh run download <RUN_ID> --repo youtalk/carla -n benchmark-report -D /tmp/ubicloud-bench
cat /tmp/ubicloud-bench/report.md
```
Expected: the full markdown table plus the `**Recommended (Balanced rule): ubicloud-standard-<N>**` line. Record `<N>` as `WINNER` and keep the table text for Task 5.

---

## Task 5: Write the results report and apply the winner

**Files:**
- Create: `docs/ci-ubicloud-benchmark.md`
- Modify: `.github/workflows/_ci-ubicloud.yml`

- [ ] **Step 1: Write the results document**

Create `docs/ci-ubicloud-benchmark.md`. Use this exact skeleton and fill the
bracketed parts from the `report.md` captured in Task 4 Step 5:

```markdown
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

## Results

[Paste the markdown table from report.md here.]

## Analysis

- Cost-optimal size: [from report.md].
- Recommended size (Balanced rule): [WINNER].
- [2-4 sentences: how build time scaled with vCPU count, where parallel
  efficiency dropped off, and why the Balanced rule selected WINNER —
  reference the speedup / cost-vs-8 columns.]

## Decision

The production reusable workflow `_ci-ubicloud.yml` default `vcpu` is set to
**[WINNER]**. `ubicloud_dev.yml` and `ubicloud_pr.yml` inherit this default.

## Caveats

- 1 run per size: no variance data. A single slow run could shift the ranking.
  Re-run the `ubicloud_benchmark.yml` workflow to collect more samples.
- Measures the LibCarla + PythonAPI build only (`BUILD_CARLA_UNREAL=OFF`),
  matching the current Ubicloud CI scope.
```

- [ ] **Step 2: Set the winner as the production default**

In `.github/workflows/_ci-ubicloud.yml`, change the `vcpu` input default from
`"30"` to `"<WINNER>"`. The block becomes:

```yaml
      vcpu:
        type: string
        required: false
        default: "<WINNER>"
```

(Replace `<WINNER>` with the actual number, e.g. `"16"`. If the winner is `30`,
the file is unchanged — note that in the commit message and skip the edit.)

- [ ] **Step 3: Verify the workflow still parses**

Run:
```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/_ci-ubicloud.yml')); print('OK')"
```
Expected: prints `OK`.

- [ ] **Step 4: Re-run the report script test (regression guard)**

Run:
```bash
bash Util/Tools/test_ubicloud_benchmark_report.sh
```
Expected: `ALL TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add docs/ci-ubicloud-benchmark.md .github/workflows/_ci-ubicloud.yml
git commit -s -m "ci(ubicloud): set production runner to winning vCPU size from benchmark"
```

- [ ] **Step 6: Push**

```bash
git push origin ci/ubicloud-vcpu-benchmark
```

---

## Done

A PR from `ci/ubicloud-vcpu-benchmark` into `ue5-dev` can now be opened manually
using the repository PR template (`.github/pull_request_template.md`). Do not
auto-create it.

## Plan Self-Review Notes

- **Spec coverage:** `vcpu` parameterization (Task 1) ✓; benchmark matrix 8/16/30 + Ubuntu 24.04 + report job (Task 3) ✓; cost model & metrics (Task 2 script) ✓; Balanced decision rule (Task 2 script) ✓; run on `origin` (Task 4) ✓; results report doc + apply winner (Task 5) ✓; verification = dispatch + populated table (Task 4 Steps 4-5) ✓.
- **Naming consistency:** artifact `bench-<vcpu>` and file `bench.json` produced in Task 1, consumed by the script in Task 2 and downloaded in Task 3 — consistent. Job name pattern `vCPU <n> /` set in Task 3 (`name: vCPU ${{ matrix.vcpu }}`) and matched by the script regex in Task 2 — consistent.
- **No placeholders:** the only intentionally dynamic values are the Task 4/5 measurement results, which cannot exist before the run; they are clearly marked as fill-from-results.
