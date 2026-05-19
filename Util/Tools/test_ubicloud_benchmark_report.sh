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
