#!/usr/bin/env bash
# Cook the CES 2027 CARLA package: Town04_Opt only, native ROS 2 on. Writes
# ces2027-package-sha.txt into the package so a run script can print which fork
# commit the package came from; a package silently misses every simulator fix
# made after it was cooked. Daylight is set at runtime through CARLA's weather
# API rather than baked into the map, because the map-authoring tool does not
# work on UE 5.8.
#
#   cook-ces2027.sh --check   validate the environment, build nothing
#   cook-ces2027.sh           configure + package (hours)
#
# Markers: COOK_CHECK_PASS | COOK_PASS package=<dir> sha=<sha> | COOK_FAIL reason=<slug>
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MAP=/Game/Carla/Maps/Town04_Opt
fail() { echo "COOK_FAIL reason=$1"; exit 1; }

UE="${CARLA_UNREAL_ENGINE_PATH:-}"
[ -n "$UE" ] && [ -x "$UE/Engine/Binaries/Linux/UnrealEditor" ] || fail engine_path
[ "${CARLA_CCACHE:-}" = 1 ] || fail ccache_env_unset
PY=$(command -v python3.12 || true); [ -n "$PY" ] || fail python312_missing
command -v cmake >/dev/null || fail cmake_missing
command -v ninja >/dev/null || fail ninja_missing
if [ "${1:-}" = --check ]; then echo "COOK_CHECK_PASS engine=$UE python=$PY"; exit 0; fi

cd "$ROOT" || fail chdir
sha=$(git rev-parse HEAD) || fail sha
[ -z "$(git status --porcelain)" ] || sha="$sha-dirty"
log=Build/cook-ces2027-$(date +%Y%m%d-%H%M%S).log
echo "cook: sha=$sha log=$log"

# 1. configure + package
cmake --preset Release -DENABLE_ROS2=ON -DCARLA_MAPS_TO_COOK="$MAP" \
  -DPython3_EXECUTABLE="$PY" >> "$log" 2>&1 || fail configure
cmake --build Build/Release --target package >> "$log" 2>&1 || fail package

pkg=$ROOT/Build/Release/Package/Carla-0.10.0-Linux-Shipping
[ -x "$pkg/CarlaUnreal.sh" ] || fail no_launcher
ls "$pkg"/PythonAPI/carla/dist/carla-*cp312*.whl >/dev/null 2>&1 || fail no_cp312_wheel
echo "$sha" > "$pkg/ces2027-package-sha.txt" || fail sha_write
echo "COOK_PASS package=$pkg sha=$sha"
