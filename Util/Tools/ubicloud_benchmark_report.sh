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
  [[ -n "$start" && -n "$end" ]] || { echo "ERROR: no job entry found for vCPU $v in $JOBS_JSON" >&2; exit 1; }
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
echo "| runner | build-phase | billed job time | billed min | cost/run | speedup vs 8 | parallel eff. | cost vs 8 |"
echo "|:-------|------------:|----------------:|-----------:|---------:|-------------:|--------------:|----------:|"
for v in "${VCPUS[@]}"; do
  bs="${BUILD_S[$v]}"
  js="${JOB_S[$v]}"
  speedup="$(awk -v b="$base_time" -v j="$js" 'BEGIN{printf "%.2f", b/j}')"
  eff="$(awk -v sp="$speedup" -v v="$v" 'BEGIN{printf "%.2f", sp/(v/8.0)}')"
  cratio="$(awk -v c="${COST[$v]}" -v b="$base_cost" 'BEGIN{printf "%.2f", c/b}')"
  printf "| ubicloud-standard-%s | %dm%02ds | %dm%02ds | %s | \$%s | %sx | %s | %sx |\n" \
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

# If several larger sizes qualify, the loop runs to completion so the largest
# one wins (maximize speed within the cost tolerance).
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
