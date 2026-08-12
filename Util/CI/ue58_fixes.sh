#!/usr/bin/env bash
#
# Apply the candidate fixes for the five UE 5.8 issues reported against
# PR #9826 (carla-simulator/carla). Each fix is applied individually so the
# verification job can prove the failure first and the fix second.
#
# Usage: ue58_fixes.sh <fix-id> [workspace]
#   fix-id: toolchain | carlaengine | streetmap | scalability | pak
#
# The script is idempotent: re-applying an already-applied fix is a no-op.

set -euo pipefail

FIX=${1:?usage: ue58_fixes.sh <toolchain|carlaengine|streetmap|scalability|pak> [workspace]}
WS=${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}

log () { echo "[ue58_fixes] $*"; }

case "$FIX" in

# ---------------------------------------------------------------- issue #1 --
# file(GLOB) returns candidates in ascending order and the loop takes the
# first directory, so an engine upgraded in place keeps its old SDK
# (v23_clang-18.1.0) and wins over the new one (v26_clang-20.1.8). Only the
# new SDK ships lib64/libc++.a, which the 5.8 sysroot fallback now needs.
# Walk the candidates newest-first and prefer one that actually carries
# libc++.a, falling back to plain directory order for UE <= 5.5 layouts where
# libc++ still lives under ThirdParty/Unix/LibCxx.
toolchain)
  f=$WS/CMake/Toolchain.cmake
  grep -q 'UE_SYSROOT_CANDIDATES_SORTED' "$f" && { log "toolchain: already applied"; exit 0; }
  python3 - "$f" <<'PY'
import sys
path = sys.argv[1]
src = open(path).read()
old = """set (UE_SYSROOT_CANDIDATE)
foreach (CANDIDATE ${UE_SYSROOT_CANDIDATES})
\tif (IS_DIRECTORY ${CANDIDATE})
\t\tset (UE_SYSROOT_CANDIDATE ${CANDIDATE})
\t\tbreak ()
\tendif ()
endforeach ()
"""
new = """# file (GLOB) yields ascending order, so an engine that kept an older SDK
# alongside the current one (e.g. v23_clang-18.1.0 next to v26_clang-20.1.8)
# would match the stale directory first. Walk newest-first, and prefer a
# candidate that actually ships libc++.a, which the UE 5.8 sysroot fallback
# below depends on. UE <= 5.5 sysroots carry no libc++.a - those fall through
# to the second pass, which only requires the directory to exist.
set (UE_SYSROOT_CANDIDATES_SORTED ${UE_SYSROOT_CANDIDATES})
list (SORT UE_SYSROOT_CANDIDATES_SORTED)
list (REVERSE UE_SYSROOT_CANDIDATES_SORTED)

set (UE_SYSROOT_CANDIDATE)
foreach (CANDIDATE ${UE_SYSROOT_CANDIDATES_SORTED})
\tif (IS_DIRECTORY ${CANDIDATE} AND EXISTS ${CANDIDATE}/lib64/libc++.a)
\t\tset (UE_SYSROOT_CANDIDATE ${CANDIDATE})
\t\tbreak ()
\tendif ()
endforeach ()

if (NOT UE_SYSROOT_CANDIDATE)
\tforeach (CANDIDATE ${UE_SYSROOT_CANDIDATES_SORTED})
\t\tif (IS_DIRECTORY ${CANDIDATE})
\t\t\tset (UE_SYSROOT_CANDIDATE ${CANDIDATE})
\t\t\tbreak ()
\t\tendif ()
\tendforeach ()
endif ()
"""
assert old in src, "Toolchain.cmake sysroot selection loop not found verbatim"
open(path, "w").write(src.replace(old, new))
PY
  log "toolchain: applied"
  ;;

# ---------------------------------------------------------------- issue #2 --
# GEngine / GEngine->GameViewport are used by the -RenderOffScreen commit but
# CarlaEngine.cpp never includes their headers. Under 5.8 IWYU this only
# compiles when a unity blob happens to pull them in.
carlaengine)
  f=$WS/Unreal/CarlaUnreal/Plugins/Carla/Source/Carla/Game/CarlaEngine.cpp
  grep -q '#include "Engine/Engine.h"' "$f" && { log "carlaengine: already applied"; exit 0; }
  python3 - "$f" <<'PY'
import sys
path = sys.argv[1]
src = open(path).read()
anchor = '#include "Misc/App.h"\n'
assert anchor in src, "CarlaEngine.cpp include anchor not found"
add = '#include "Engine/Engine.h"\n#include "Engine/GameViewportClient.h"\n'
open(path, "w").write(src.replace(anchor, anchor + add, 1))
PY
  log "carlaengine: applied"
  ;;

# ---------------------------------------------------------------- issue #3 --
# TArray::SetNum's bool overload became EAllowShrinking. StreetMapRuntime is
# an Editor-type module, so only the editor target (which packaging needs)
# compiles it. NOTE: this file belongs to carla-simulator/StreetMap, which
# CMake downloads into Plugins/StreetMap - the fix must land in that repo.
streetmap)
  f=$WS/Unreal/CarlaUnreal/Plugins/StreetMap/Source/StreetMapRuntime/Private/StreetMapComponent.cpp
  [ -f "$f" ] || { log "streetmap: $f not present (StreetMap not downloaded yet)"; exit 1; }
  grep -q 'EAllowShrinking::No' "$f" && { log "streetmap: already applied"; exit 0; }
  sed -i \
    -e 's/TempPoints\.SetNum( Building\.BuildingPoints\.Num(), false )/TempPoints.SetNum( Building.BuildingPoints.Num(), EAllowShrinking::No )/' \
    -e 's/TempPoints\.SetNum( 4, false )/TempPoints.SetNum( 4, EAllowShrinking::No )/' \
    -e 's/TempIndices\.SetNum( 6, false )/TempIndices.SetNum( 6, EAllowShrinking::No )/' \
    "$f"
  grep -c 'EAllowShrinking::No' "$f"
  log "streetmap: applied"
  ;;

# ---------------------------------------------------------------- issue #4 --
# Both CVars are FAutoConsoleVariableDeprecated in 5.8. Reading them raises a
# handled ensure per site during Scalability::InitScalabilitySystem(), and the
# cook counts every ensure line as an error.
scalability)
  f=$WS/Unreal/CarlaUnreal/Config/DefaultScalability.ini
  grep -q 'r.TranslucencyLightingVolume.Dim' "$f" && { log "scalability: already applied"; exit 0; }
  sed -i \
    -e 's/^r\.TranslucencyLightingVolumeDim=/r.TranslucencyLightingVolume.Dim=/' \
    -e 's/^r\.TranslucencyVolumeBlur=/r.TranslucencyLightingVolume.Blur=/' \
    "$f"
  grep -n 'TranslucencyLightingVolume' "$f"
  log "scalability: applied"
  ;;

# ---------------------------------------------------------------- issue #5 --
# 5.8 cooks into the Zen store, so a -stage with no -pak/-iostore finds no
# loose files and archives binaries only, silently exiting 0.
pak)
  f=$WS/Unreal/CMakeLists.txt
  grep -q -- '-iostore' "$f" && { log "pak: already applied"; exit 0; }
  python3 - "$f" <<'PY'
import sys
path = sys.argv[1]
src = open(path).read()
old = """      -cook
      -stage
      -archive
      -package
      -iterate
"""
new = """      -cook
      -stage
      -archive
      -package
      -iterate
      # UE 5.8 cooks into the Zen store; without -pak/-iostore the stage step
      # finds no loose cooked files and silently archives binaries only.
      -pak
      -iostore
"""
assert old in src, "BuildCookRun argument block not found verbatim"
assert src.count(old) == 1, "BuildCookRun argument block matched more than once"
open(path, "w").write(src.replace(old, new))
PY
  log "pak: applied"
  ;;

*)
  echo "unknown fix id: $FIX" >&2
  exit 2
  ;;
esac
