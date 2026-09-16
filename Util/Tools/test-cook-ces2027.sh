#!/usr/bin/env bash
# cook-ces2027.sh --check must refuse a missing engine path and accept a good one,
# without touching cmake. Run from anywhere.
set -u
here=$(cd "$(dirname "$0")" && pwd)
s="$here/cook-ces2027.sh"
fail() { echo "TEST_FAIL test-cook-ces2027 reason=$1"; exit 1; }
[ -x "$s" ] || fail script_missing
out=$(CARLA_UNREAL_ENGINE_PATH=/nonexistent bash "$s" --check 2>&1); rc=$?
[ "$rc" -ne 0 ] || fail bad_engine_accepted
grep -q 'COOK_FAIL reason=engine_path' <<<"$out" || fail bad_engine_reason
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/Engine/Binaries/Linux" && : > "$tmp/Engine/Binaries/Linux/UnrealEditor" && chmod +x "$tmp/Engine/Binaries/Linux/UnrealEditor"
out=$(CARLA_UNREAL_ENGINE_PATH="$tmp" CARLA_CCACHE=1 bash "$s" --check 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail good_env_rejected
grep -q '^COOK_CHECK_PASS' <<<"$out" || fail no_pass_marker
echo "TEST_PASS test-cook-ces2027"
