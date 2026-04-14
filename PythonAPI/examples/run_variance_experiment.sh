#!/bin/bash
# CARLA Benchmark Variance Experiment
# Usage: bash run_variance_experiment.sh <ue-branch> <output-json>
# Example:
#   bash run_variance_experiment.sh ue5-dev-carla variance_v3_pre.json
#   bash run_variance_experiment.sh merge/5.5.4-patch variance_v3_post.json

set -euo pipefail

UE_BRANCH="${1:?Usage: $0 <ue-branch> <output-json>}"
OUTPUT="${2:?Usage: $0 <ue-branch> <output-json>}"

CARLA_DIR="/home/youtalk/src/carla"
UE_DIR="/home/youtalk/src/UnrealEngine"
PACKAGE_DIR="$CARLA_DIR/Build/Release/Package/Carla-0.10.0-Linux-Shipping"
SERVER_BIN="$PACKAGE_DIR/Linux/CarlaUnreal.sh"
BENCHMARK="$CARLA_DIR/PythonAPI/examples/benchmark_variance.py"

RUNS=20
DURATION=30
MAP="Town10HD_Opt"

echo "=========================================="
echo "CARLA Variance Experiment"
echo "  UE Branch: $UE_BRANCH"
echo "  Output: $OUTPUT"
echo "  Runs: $RUNS x 5 scenarios x ${DURATION}s"
echo "=========================================="

# 1. Stop any existing server
echo ""
echo "[1/7] Stopping existing server..."
pkill -9 -f CarlaUnreal-Linux-Shipping 2>/dev/null || true
sleep 2

# 2. Switch UE branch
echo ""
echo "[2/7] Switching UE to branch: $UE_BRANCH"
cd "$UE_DIR"
git checkout "$UE_BRANCH"
echo "  Branch: $(git branch --show-current)"
echo "  HEAD: $(git rev-parse --short HEAD)"

# 3. Build CARLA
echo ""
echo "[3/7] Building CARLA (cmake configure + package)..."
cd "$CARLA_DIR"
cmake --preset Release -DBUILD_CARLA_UNREAL=ON 2>&1 | tail -3
echo "  Configure done. Building package..."
cmake --build Build/Release --target package 2>&1 | tail -5
echo "  Build complete."

# 4. Start server
echo ""
echo "[4/7] Starting server..."
"$SERVER_BIN" -RenderOffScreen -nosound -carla-rpc-port=2000 &>/dev/null &
SERVER_SHELL_PID=$!
echo "  Shell PID: $SERVER_SHELL_PID"

# Wait for connection
echo "  Waiting for server to accept connections..."
for i in $(seq 1 30); do
    if python3 -c "import carla; c=carla.Client('127.0.0.1',2000); c.set_timeout(5); c.get_world()" 2>/dev/null; then
        echo "  Connected!"
        break
    fi
    if [ "$i" -eq 30 ]; then
        echo "  ERROR: Server did not start within 30s"
        exit 1
    fi
    sleep 1
done

# 5. Verify build identity
echo ""
echo "[5/7] Verifying build identity..."
THREADS=$(python3 -c "
import psutil
for p in psutil.process_iter(['pid','name','memory_info']):
    try:
        if 'CarlaUnreal-Linux-Shipping' in (p.info['name'] or ''):
            print(psutil.Process(p.info['pid']).num_threads())
            break
    except: pass
" 2>/dev/null)
echo "  Server threads at startup: $THREADS"
echo "  UE branch: $(cd "$UE_DIR" && git branch --show-current)"
echo "  UE HEAD: $(cd "$UE_DIR" && git rev-parse --short HEAD)"

# 6. Run benchmark
echo ""
echo "[6/7] Running benchmark: $RUNS runs x 5 scenarios x ${DURATION}s each..."
echo "  Output: $CARLA_DIR/$OUTPUT"
echo "  Start: $(date)"
cd "$CARLA_DIR"
PYTHONUNBUFFERED=1 python3 "$BENCHMARK" \
    --runs "$RUNS" \
    --duration "$DURATION" \
    --map "$MAP" \
    --output "$OUTPUT" \
    2>&1
echo "  End: $(date)"

# 7. Stop server
echo ""
echo "[7/7] Stopping server..."
pkill -9 -f CarlaUnreal-Linux-Shipping 2>/dev/null || true
sleep 2

echo ""
echo "=========================================="
echo "Experiment complete: $OUTPUT"
echo "=========================================="
