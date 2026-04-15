#!/usr/bin/env python3
"""
CARLA Benchmark Variance Test v3

Runs 5 scenarios x N repetitions to measure natural run-to-run fluctuation.
Each run is a separate subprocess to avoid sync-mode hangs.

Scenarios:
  idle                    - empty world (lightest baseline)
  traffic_50v_30w         - 50 vehicles + 30 walkers (traffic simulation)
  sensors_ego             - 1 vehicle + RGB+Depth+LiDAR+Radar (perception dev)
  combined_30v_20w_sensors - 30v + 20w + ego sensors (full AD pipeline)
  max_stress              - night+rain+fog + 50v + 30w + ego multi-camera (heaviest)

Usage:
  # Orchestrator (launches subprocesses):
  python3 benchmark_variance.py --runs 20 --duration 30 --output results.json

  # Single scenario (called internally):
  python3 benchmark_variance.py --worker --worker-scenario idle --duration 30
"""

import argparse
import json
import math
import os
import subprocess
import sys
import time
from datetime import datetime

ALL_SCENARIOS = [
    "idle",
    "traffic_50v_30w",
    "sensors_ego",
    "combined_30v_20w_sensors",
    "max_stress",
]

FOCUS_METRICS = ["vram_used_mb", "server_rss_mb", "server_vms_mb", "server_threads"]

ALL_METRICS = [
    "gpu_util_pct", "vram_used_mb", "gpu_temp_c", "power_draw_w",
    "cpu_util_pct", "ram_used_mb",
    "server_rss_mb", "server_vms_mb", "server_cpu_pct", "server_threads",
    "server_fps",
]


# ═══════════════════════════════════════════════════════════════════
# Worker: runs one scenario once in an isolated process
# ═══════════════════════════════════════════════════════════════════

def run_worker(host, port, map_name, scenario_key, duration):
    import carla
    import psutil

    client = carla.Client(host, port)
    client.set_timeout(30.0)

    # Ensure async before map load
    try:
        w = client.get_world()
        s = w.get_settings()
        if s.synchronous_mode:
            s.synchronous_mode = False
            s.fixed_delta_seconds = None
            w.apply_settings(s)
            time.sleep(1)
    except Exception:
        pass

    world = client.load_world(map_name)
    time.sleep(5)

    settings = world.get_settings()
    settings.synchronous_mode = True
    settings.fixed_delta_seconds = 0.05
    settings.no_rendering_mode = False
    world.apply_settings(settings)

    tm = client.get_trafficmanager()
    tm.set_synchronous_mode(True)

    # Find server process
    server_proc = None
    for proc in psutil.process_iter(["pid", "name", "memory_info"]):
        try:
            name = proc.info["name"] or ""
            if "CarlaUnreal-Linux-Shipping" in name:
                rss = proc.info["memory_info"].rss if proc.info["memory_info"] else 0
                if rss > 100 * 1024 * 1024:
                    server_proc = psutil.Process(proc.info["pid"])
                    break
        except Exception:
            continue

    bp_lib = world.get_blueprint_library()
    actor_ids = []

    try:
        # ── Setup scenario ──
        actor_ids = _setup_scenario(scenario_key, client, world, bp_lib)

        # ── Warmup (5s = 100 ticks) ──
        for _ in range(100):
            world.tick(10.0)

        # ── Prime CPU counters ──
        psutil.cpu_percent(interval=None)
        if server_proc:
            try:
                server_proc.cpu_percent(interval=None)
            except Exception:
                pass
        world.tick(10.0)
        time.sleep(0.1)

        # ── Collect samples ──
        samples = []
        tick_count = 1
        end_time = time.time() + duration
        while time.time() < end_time:
            world.tick(10.0)
            tick_count += 1
            if tick_count % 10 == 0:  # ~0.5s interval
                sample = _collect_one_sample(server_proc, world)
                samples.append(sample)

        # ── Summarize ──
        summary = {}
        for key in ALL_METRICS:
            vals = [s[key] for s in samples if key in s]
            if vals:
                vals_sorted = sorted(vals)
                n = len(vals)
                summary[key] = {
                    "mean": round(sum(vals) / n, 2),
                    "min": round(vals_sorted[0], 2),
                    "max": round(vals_sorted[-1], 2),
                }
        summary["_samples"] = len(samples)
        summary["_ticks"] = tick_count

        print(json.dumps(summary))

    finally:
        if actor_ids:
            batch = [carla.command.DestroyActor(aid) for aid in actor_ids]
            client.apply_batch_sync(batch, True)
            for _ in range(20):
                world.tick(10.0)
        settings = world.get_settings()
        settings.synchronous_mode = False
        settings.fixed_delta_seconds = None
        world.apply_settings(settings)


def _collect_one_sample(server_proc, world):
    import psutil
    sample = {}
    # GPU
    try:
        result = subprocess.run(
            ["nvidia-smi",
             "--query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu,power.draw",
             "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=5,
        )
        if result.returncode == 0:
            vals = [v.strip() for v in result.stdout.strip().split(",")]
            sample["gpu_util_pct"] = float(vals[0])
            sample["vram_used_mb"] = float(vals[1])
            sample["gpu_temp_c"] = float(vals[3])
            sample["power_draw_w"] = float(vals[4])
    except Exception:
        pass
    # CPU / Memory
    mem = psutil.virtual_memory()
    sample["cpu_util_pct"] = psutil.cpu_percent(interval=None)
    sample["ram_used_mb"] = mem.used / (1024 * 1024)
    if server_proc:
        try:
            pm = server_proc.memory_info()
            sample["server_rss_mb"] = pm.rss / (1024 * 1024)
            sample["server_vms_mb"] = pm.vms / (1024 * 1024)
            sample["server_cpu_pct"] = server_proc.cpu_percent(interval=None)
            sample["server_threads"] = server_proc.num_threads()
        except Exception:
            pass
    # FPS
    try:
        dt = world.get_snapshot().timestamp.delta_seconds
        if dt > 0:
            sample["server_fps"] = 1.0 / dt
    except Exception:
        pass
    return sample


# ── Scenario setup ──────────────────────────────────────────────

def _setup_scenario(key, client, world, bp_lib):
    import carla
    if key == "idle":
        return []

    elif key == "traffic_50v_30w":
        vehicles = _spawn_vehicles(client, world, bp_lib, 50)
        walkers = _spawn_walkers(client, world, bp_lib, 30)
        return vehicles + walkers

    elif key == "sensors_ego":
        vehicles = _spawn_vehicles(client, world, bp_lib, 1)
        sensors = _attach_standard_sensors(world, bp_lib, vehicles[0]) if vehicles else []
        return vehicles + sensors

    elif key == "combined_30v_20w_sensors":
        vehicles = _spawn_vehicles(client, world, bp_lib, 30)
        walkers = _spawn_walkers(client, world, bp_lib, 20)
        sensors = _attach_standard_sensors(world, bp_lib, vehicles[0]) if vehicles else []
        return vehicles + walkers + sensors

    elif key == "max_stress":
        weather = carla.WeatherParameters(
            cloudiness=90.0, precipitation=80.0, precipitation_deposits=80.0,
            wind_intensity=80.0, sun_altitude_angle=-30.0,
            fog_density=50.0, fog_distance=30.0, wetness=100.0,
        )
        world.set_weather(weather)
        vehicles = _spawn_vehicles(client, world, bp_lib, 50)
        walkers = _spawn_walkers(client, world, bp_lib, 30)
        sensors = _attach_heavy_sensors(world, bp_lib, vehicles[0]) if vehicles else []
        return vehicles + walkers + sensors

    return []


def _spawn_vehicles(client, world, bp_lib, count):
    import carla
    blueprints = [b for b in bp_lib.filter("vehicle.*") if b.get_attribute("base_type") == "car"]
    spawn_points = world.get_map().get_spawn_points()
    batch = []
    for i in range(min(count, len(spawn_points))):
        bp = blueprints[i % len(blueprints)]
        if bp.has_attribute("color"):
            bp.set_attribute("color", "0,0,0")
        batch.append(
            carla.command.SpawnActor(bp, spawn_points[i]).then(
                carla.command.SetAutopilot(carla.command.FutureActor, True)
            )
        )
    results = client.apply_batch_sync(batch, True)
    return [r.actor_id for r in results if not r.error]


def _spawn_walkers(client, world, bp_lib, count):
    import carla
    walker_bps = bp_lib.filter("walker.pedestrian.*")
    spawn_points = []
    for _ in range(count):
        loc = world.get_random_location_from_navigation()
        if loc:
            spawn_points.append(carla.Transform(loc))

    batch = [carla.command.SpawnActor(walker_bps[i % len(walker_bps)], sp)
             for i, sp in enumerate(spawn_points)]
    results = client.apply_batch_sync(batch, True)
    walkers = [r.actor_id for r in results if not r.error]

    controller_bp = bp_lib.find("controller.ai.walker")
    ctrl_batch = [carla.command.SpawnActor(controller_bp, carla.Transform(), wid)
                  for wid in walkers]
    ctrl_results = client.apply_batch_sync(ctrl_batch, True)
    controllers = [r.actor_id for r in ctrl_results if not r.error]

    world.tick(10.0)
    for cid in controllers:
        try:
            ctrl = world.get_actor(cid)
            if ctrl:
                ctrl.start()
                ctrl.go_to_location(world.get_random_location_from_navigation())
                ctrl.set_max_speed(1.5)
        except Exception:
            pass

    return walkers + controllers


def _attach_standard_sensors(world, bp_lib, vehicle_id):
    """Standard sensor suite: RGB + Depth + LiDAR + Radar."""
    import carla
    vehicle = world.get_actor(vehicle_id)
    if not vehicle:
        return []
    configs = [
        ("sensor.camera.rgb",
         {"image_size_x": "1920", "image_size_y": "1080", "fov": "90"},
         carla.Transform(carla.Location(x=1.5, z=2.4))),
        ("sensor.camera.depth",
         {"image_size_x": "1920", "image_size_y": "1080", "fov": "90"},
         carla.Transform(carla.Location(x=1.5, z=2.4))),
        ("sensor.lidar.ray_cast",
         {"range": "50", "channels": "64", "points_per_second": "1200000", "rotation_frequency": "20"},
         carla.Transform(carla.Location(z=2.5))),
        ("sensor.other.radar", {},
         carla.Transform(carla.Location(x=2.0, z=1.0))),
    ]
    return _spawn_sensors(world, bp_lib, vehicle, configs)


def _attach_heavy_sensors(world, bp_lib, vehicle_id):
    """Heavy sensor suite: 6xRGB + 2xDepth + LiDAR + SemanticLiDAR + Radar."""
    import carla
    vehicle = world.get_actor(vehicle_id)
    if not vehicle:
        return []

    cam_attrs = {"image_size_x": "1920", "image_size_y": "1080", "fov": "90"}
    configs = [
        # 6x RGB: front, rear, left, right, front-left, front-right
        ("sensor.camera.rgb", cam_attrs,
         carla.Transform(carla.Location(x=1.5, z=2.4))),                             # front
        ("sensor.camera.rgb", cam_attrs,
         carla.Transform(carla.Location(x=-1.5, z=2.4), carla.Rotation(yaw=180))),   # rear
        ("sensor.camera.rgb", cam_attrs,
         carla.Transform(carla.Location(y=-0.5, z=2.4), carla.Rotation(yaw=-90))),   # left
        ("sensor.camera.rgb", cam_attrs,
         carla.Transform(carla.Location(y=0.5, z=2.4), carla.Rotation(yaw=90))),     # right
        ("sensor.camera.rgb", cam_attrs,
         carla.Transform(carla.Location(x=1.2, y=-0.5, z=2.4), carla.Rotation(yaw=-45))),  # front-left
        ("sensor.camera.rgb", cam_attrs,
         carla.Transform(carla.Location(x=1.2, y=0.5, z=2.4), carla.Rotation(yaw=45))),    # front-right
        # 2x Depth: front, rear
        ("sensor.camera.depth", cam_attrs,
         carla.Transform(carla.Location(x=1.5, z=2.4))),
        ("sensor.camera.depth", cam_attrs,
         carla.Transform(carla.Location(x=-1.5, z=2.4), carla.Rotation(yaw=180))),
        # LiDAR 64ch
        ("sensor.lidar.ray_cast",
         {"range": "50", "channels": "64", "points_per_second": "1200000", "rotation_frequency": "20"},
         carla.Transform(carla.Location(z=2.5))),
        # Semantic LiDAR 32ch
        ("sensor.lidar.ray_cast_semantic",
         {"range": "50", "channels": "32", "points_per_second": "600000", "rotation_frequency": "20"},
         carla.Transform(carla.Location(z=2.5))),
        # Radar
        ("sensor.other.radar", {},
         carla.Transform(carla.Location(x=2.0, z=1.0))),
    ]
    return _spawn_sensors(world, bp_lib, vehicle, configs)


def _spawn_sensors(world, bp_lib, vehicle, configs):
    sensors = []
    for sensor_type, attrs, transform in configs:
        bp = bp_lib.find(sensor_type)
        if not bp:
            continue
        for k, v in attrs.items():
            if bp.has_attribute(k):
                bp.set_attribute(k, v)
        sensor = world.spawn_actor(bp, transform, attach_to=vehicle)
        sensor.listen(lambda data: None)
        sensors.append(sensor.id)
    return sensors


# ═══════════════════════════════════════════════════════════════════
# Orchestrator: launches worker subprocesses
# ═══════════════════════════════════════════════════════════════════

def compute_stats(values):
    if not values:
        return None
    n = len(values)
    mean = sum(values) / n
    variance = sum((x - mean) ** 2 for x in values) / (n - 1) if n > 1 else 0
    stddev = math.sqrt(variance)
    values_sorted = sorted(values)
    return {
        "n": n,
        "min": round(values_sorted[0], 2),
        "max": round(values_sorted[-1], 2),
        "mean": round(mean, 2),
        "stddev": round(stddev, 2),
        "range": round(values_sorted[-1] - values_sorted[0], 2),
        "values": [round(v, 2) for v in values],
    }


def _is_server_alive(host, port):
    """Check if CARLA server is responding."""
    try:
        result = subprocess.run(
            [sys.executable, "-c",
             f"import carla; c=carla.Client('{host}',{port}); c.set_timeout(5); c.get_world()"],
            capture_output=True, timeout=10,
        )
        return result.returncode == 0
    except Exception:
        return False


def _restart_server(server_bin, host, port):
    """Kill existing server and start a new one. Returns True if successful."""
    import signal
    print("    >>> Restarting server...", flush=True)
    # Kill existing
    subprocess.run(["pkill", "-9", "-f", "CarlaUnreal-Linux-Shipping"],
                   capture_output=True)
    time.sleep(3)
    # Start new
    subprocess.Popen(
        [server_bin, "-RenderOffScreen", "-nosound", f"-carla-rpc-port={port}"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    # Wait for connection
    for _ in range(30):
        if _is_server_alive(host, port):
            print("    >>> Server restarted OK", flush=True)
            return True
        time.sleep(1)
    print("    >>> Server restart FAILED", flush=True)
    return False


def main():
    parser = argparse.ArgumentParser(description="CARLA Benchmark Variance Test v3")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=2000, type=int)
    parser.add_argument("--runs", default=20, type=int)
    parser.add_argument("--duration", default=30, type=int)
    parser.add_argument("--map", default="Town10HD_Opt")
    parser.add_argument("--output", default="benchmark_variance.json")
    parser.add_argument("--scenarios", nargs="*", default=ALL_SCENARIOS, choices=ALL_SCENARIOS)
    parser.add_argument("--server-bin", default=None,
                        help="Path to CarlaUnreal.sh for auto-restart on crash")
    # Worker mode (internal)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--worker-scenario", type=str, help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.worker:
        run_worker(args.host, args.port, args.map, args.worker_scenario, args.duration)
        return

    # ── Orchestrator ──
    print("CARLA Benchmark Variance Test v3", flush=True)
    print(f"  Scenarios: {args.scenarios}", flush=True)
    print(f"  Runs per scenario: {args.runs}", flush=True)
    print(f"  Duration per run: {args.duration}s", flush=True)
    print(f"  Map: {args.map}", flush=True)
    print(f"  Server auto-restart: {'YES' if args.server_bin else 'NO'}", flush=True)
    print(f"  Each run = independent subprocess", flush=True)
    print(flush=True)

    all_results = {
        "timestamp": datetime.now().isoformat(),
        "runs": args.runs,
        "duration": args.duration,
        "map": args.map,
        "scenarios": {},
    }

    script_path = os.path.abspath(__file__)
    worker_timeout = args.duration + 120
    consecutive_errors = 0

    for scenario_key in args.scenarios:
        print(f"\n{'='*60}", flush=True)
        print(f"Scenario: {scenario_key}", flush=True)
        print(f"{'='*60}", flush=True)

        run_results = []
        restarts = 0
        timeouts = 0
        errors = 0
        for i in range(args.runs):
            print(f"\n  --- Run {i+1}/{args.runs} ---", flush=True)

            # Check if server needs restart before this run
            if consecutive_errors >= 2 and args.server_bin:
                if not _restart_server(args.server_bin, args.host, args.port):
                    print("    Server could not be restarted, aborting scenario", flush=True)
                    break
                restarts += 1
                consecutive_errors = 0

            cmd = [
                sys.executable, script_path,
                "--worker",
                "--worker-scenario", scenario_key,
                "--host", args.host,
                "--port", str(args.port),
                "--map", args.map,
                "--duration", str(args.duration),
            ]
            try:
                result = subprocess.run(
                    cmd, capture_output=True, text=True,
                    timeout=worker_timeout,
                )
                if result.returncode != 0:
                    print(f"    ERROR: exit code {result.returncode}", flush=True)
                    stderr_tail = result.stderr.strip().split("\n")[-3:]
                    for line in stderr_tail:
                        print(f"    stderr: {line}", flush=True)
                    consecutive_errors += 1
                    errors += 1
                    # If connection refused and we have server-bin, restart immediately
                    if "Connection refused" in result.stderr and args.server_bin:
                        if _restart_server(args.server_bin, args.host, args.port):
                            restarts += 1
                            consecutive_errors = 0
                    continue

                stdout_lines = result.stdout.strip().split("\n")
                summary = json.loads(stdout_lines[-1])
                run_results.append(summary)
                consecutive_errors = 0

                for metric in FOCUS_METRICS:
                    if metric in summary:
                        print(f"    {metric}: {summary[metric]['mean']}", flush=True)

            except subprocess.TimeoutExpired:
                print(f"    TIMEOUT after {worker_timeout}s — skipping", flush=True)
                consecutive_errors += 1
                timeouts += 1
                continue
            except json.JSONDecodeError as e:
                print(f"    JSON parse error: {e}", flush=True)
                consecutive_errors += 1
                continue

        if not run_results:
            print(f"  WARNING: no successful runs for {scenario_key}", flush=True)
            continue

        # Aggregate
        print(f"\n  === Variance Summary for {scenario_key} ===", flush=True)
        aggregated = {}
        for metric in FOCUS_METRICS:
            means = [r[metric]["mean"] for r in run_results if metric in r]
            if means:
                stats = compute_stats(means)
                aggregated[metric] = stats
                print(f"    {metric}: mean={stats['mean']}, sd={stats['stddev']}, "
                      f"range={stats['range']} [{stats['min']}..{stats['max']}]", flush=True)

        print(f"    restarts={restarts}, timeouts={timeouts}, errors={errors}, "
              f"success={len(run_results)}/{args.runs}", flush=True)

        all_results["scenarios"][scenario_key] = {
            "runs": run_results,
            "variance": aggregated,
            "restarts": restarts,
            "timeouts": timeouts,
            "errors": errors,
            "successful_runs": len(run_results),
        }

    with open(args.output, "w") as f:
        json.dump(all_results, f, indent=2)

    print(f"\nResults saved to: {args.output}", flush=True)


if __name__ == "__main__":
    main()
