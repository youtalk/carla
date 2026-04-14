#!/usr/bin/env python3
"""
CARLA Benchmark Variance Test

Runs specific scenarios multiple times to measure natural fluctuation.
Each run is executed as a separate subprocess to avoid sync-mode hangs.

Usage:
    python3 benchmark_variance.py [--host H] [--port P] [--runs N] [--duration D]
"""

import argparse
import json
import math
import os
import signal
import subprocess
import sys
import time
from datetime import datetime

# ─── Single-run worker (invoked as subprocess) ──────────────────────

def run_single(host, port, map_name, scenario_key, duration):
    """Run one scenario once and print JSON result to stdout."""
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

    # Sync mode
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

    actor_ids = []
    try:
        # Setup scenario
        bp_lib = world.get_blueprint_library()

        if scenario_key == "01_idle":
            pass
        elif scenario_key == "04_sensors_single_vehicle":
            actor_ids = _spawn_vehicles(client, world, bp_lib, 1)
            if actor_ids:
                actor_ids += _attach_sensors(world, bp_lib, actor_ids[0])
        elif scenario_key == "06_rendering_stress_night_rain":
            weather = carla.WeatherParameters(
                cloudiness=90.0, precipitation=80.0, precipitation_deposits=80.0,
                wind_intensity=80.0, sun_altitude_angle=-30.0,
                fog_density=50.0, fog_distance=30.0, wetness=100.0,
            )
            world.set_weather(weather)
            actor_ids = _spawn_vehicles(client, world, bp_lib, 30)
            if actor_ids:
                actor_ids += _attach_sensors(world, bp_lib, actor_ids[0])

        # Warmup (5s = 100 ticks)
        for _ in range(100):
            world.tick(10.0)

        # Prime CPU counters
        psutil.cpu_percent(interval=None)
        if server_proc:
            try:
                server_proc.cpu_percent(interval=None)
            except Exception:
                pass
        world.tick(10.0)
        time.sleep(0.1)

        # Collect samples
        samples = []
        tick_count = 1
        end_time = time.time() + duration
        while time.time() < end_time:
            world.tick(10.0)
            tick_count += 1
            if tick_count % 10 == 0:  # sample every ~0.5s
                sample = {}
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
                try:
                    dt = world.get_snapshot().timestamp.delta_seconds
                    if dt > 0:
                        sample["server_fps"] = 1.0 / dt
                except Exception:
                    pass
                samples.append(sample)

        # Summarize
        summary = {}
        for key in ["gpu_util_pct", "vram_used_mb", "gpu_temp_c", "power_draw_w",
                     "cpu_util_pct", "ram_used_mb",
                     "server_rss_mb", "server_vms_mb", "server_cpu_pct",
                     "server_threads", "server_fps"]:
            vals = [s[key] for s in samples if key in s]
            if vals:
                vals_sorted = sorted(vals)
                n = len(vals)
                summary[key] = {
                    "mean": round(sum(vals) / n, 2),
                    "min": round(vals_sorted[0], 2),
                    "max": round(vals_sorted[-1], 2),
                }

        # Output JSON to stdout
        print(json.dumps(summary))

    finally:
        # Cleanup
        if actor_ids:
            batch = [carla.command.DestroyActor(aid) for aid in actor_ids]
            client.apply_batch_sync(batch, True)
            for _ in range(20):
                world.tick(10.0)
        # Restore async
        settings = world.get_settings()
        settings.synchronous_mode = False
        settings.fixed_delta_seconds = None
        world.apply_settings(settings)


def _spawn_vehicles(client, world, bp_lib, count):
    blueprints = [b for b in bp_lib.filter("vehicle.*") if b.get_attribute("base_type") == "car"]
    spawn_points = world.get_map().get_spawn_points()
    import carla as _carla
    batch = []
    for i in range(min(count, len(spawn_points))):
        bp = blueprints[i % len(blueprints)]
        if bp.has_attribute("color"):
            bp.set_attribute("color", "0,0,0")
        batch.append(
            _carla.command.SpawnActor(bp, spawn_points[i]).then(
                _carla.command.SetAutopilot(_carla.command.FutureActor, True)
            )
        )
    results = client.apply_batch_sync(batch, True)
    return [r.actor_id for r in results if not r.error]


def _attach_sensors(world, bp_lib, vehicle_id):
    import carla as _carla
    vehicle = world.get_actor(vehicle_id)
    if not vehicle:
        return []
    sensors = []
    configs = [
        ("sensor.camera.rgb", {"image_size_x": "1920", "image_size_y": "1080", "fov": "90"},
         _carla.Transform(_carla.Location(x=1.5, z=2.4))),
        ("sensor.camera.depth", {"image_size_x": "1920", "image_size_y": "1080", "fov": "90"},
         _carla.Transform(_carla.Location(x=1.5, z=2.4))),
        ("sensor.lidar.ray_cast", {"range": "50", "channels": "64", "points_per_second": "1200000", "rotation_frequency": "20"},
         _carla.Transform(_carla.Location(z=2.5))),
        ("sensor.other.radar", {}, _carla.Transform(_carla.Location(x=2.0, z=1.0))),
    ]
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


# ─── Orchestrator (main process) ────────────────────────────────────

SCENARIOS = ["01_idle", "04_sensors_single_vehicle", "06_rendering_stress_night_rain"]
FOCUS_METRICS = ["vram_used_mb", "server_rss_mb", "server_vms_mb", "server_threads"]
OBSERVED_DELTAS = {
    "01_idle": {"vram_used_mb": -29, "server_rss_mb": 188, "server_vms_mb": 1124, "server_threads": 14},
    "04_sensors_single_vehicle": {"vram_used_mb": 448, "server_rss_mb": 146, "server_vms_mb": 1090, "server_threads": 14},
    "06_rendering_stress_night_rain": {"vram_used_mb": 282, "server_rss_mb": -143, "server_vms_mb": 790, "server_threads": 14},
}


def compute_stats(values):
    if not values:
        return None
    n = len(values)
    mean = sum(values) / n
    variance = sum((x - mean) ** 2 for x in values) / n if n > 1 else 0
    stddev = math.sqrt(variance)
    values_sorted = sorted(values)
    return {
        "min": round(values_sorted[0], 2),
        "max": round(values_sorted[-1], 2),
        "mean": round(mean, 2),
        "stddev": round(stddev, 2),
        "range": round(values_sorted[-1] - values_sorted[0], 2),
        "values": [round(v, 2) for v in values],
    }


def main():
    parser = argparse.ArgumentParser(description="CARLA Benchmark Variance Test")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=2000, type=int)
    parser.add_argument("--runs", default=5, type=int)
    parser.add_argument("--duration", default=30, type=int)
    parser.add_argument("--map", default="Town10HD_Opt")
    parser.add_argument("--output", default="benchmark_variance.json")
    parser.add_argument("--scenarios", nargs="*", default=SCENARIOS, choices=SCENARIOS)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--worker-scenario", type=str, help=argparse.SUPPRESS)
    args = parser.parse_args()

    # Worker mode: run single scenario and exit
    if args.worker:
        run_single(args.host, args.port, args.map, args.worker_scenario, args.duration)
        return

    # Orchestrator mode
    print("CARLA Benchmark Variance Test")
    print(f"  Scenarios: {args.scenarios}")
    print(f"  Runs per scenario: {args.runs}")
    print(f"  Duration per run: {args.duration}s")
    print(f"  Map: {args.map}")
    print(f"  Each run = independent subprocess (no hang risk)")
    print()

    all_results = {
        "timestamp": datetime.now().isoformat(),
        "runs": args.runs,
        "duration": args.duration,
        "map": args.map,
        "scenarios": {},
    }

    script_path = os.path.abspath(__file__)

    for scenario_key in args.scenarios:
        print(f"\n{'='*60}")
        print(f"Scenario: {scenario_key}")
        print(f"{'='*60}")

        run_results = []
        for i in range(args.runs):
            print(f"\n  --- Run {i+1}/{args.runs} ---", flush=True)
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
                    timeout=args.duration + 120,  # generous timeout
                )
                if result.returncode != 0:
                    print(f"    ERROR: worker exit code {result.returncode}")
                    if result.stderr:
                        # Print last 3 lines of stderr
                        for line in result.stderr.strip().split("\n")[-3:]:
                            print(f"    stderr: {line}")
                    continue

                # Parse JSON from last line of stdout
                stdout_lines = result.stdout.strip().split("\n")
                summary = json.loads(stdout_lines[-1])
                run_results.append(summary)

                for metric in FOCUS_METRICS:
                    if metric in summary:
                        print(f"    {metric}: {summary[metric]['mean']}")

            except subprocess.TimeoutExpired:
                print(f"    TIMEOUT: run killed after {args.duration + 120}s")
                continue
            except json.JSONDecodeError as e:
                print(f"    ERROR: failed to parse output: {e}")
                continue

        if not run_results:
            print(f"  WARNING: no successful runs for {scenario_key}")
            continue

        # Aggregate
        print(f"\n  === Variance Summary for {scenario_key} ===")
        aggregated = {}
        for metric in FOCUS_METRICS:
            means = [r[metric]["mean"] for r in run_results if metric in r]
            if means:
                stats = compute_stats(means)
                aggregated[metric] = stats
                print(f"    {metric}:")
                print(f"      values = {stats['values']}")
                print(f"      mean   = {stats['mean']}")
                print(f"      stddev = {stats['stddev']}")
                print(f"      range  = {stats['range']} (min={stats['min']}, max={stats['max']})")

        all_results["scenarios"][scenario_key] = {
            "runs": run_results,
            "variance": aggregated,
        }

    with open(args.output, "w") as f:
        json.dump(all_results, f, indent=2)

    # Final report
    print(f"\n{'='*60}")
    print("FINAL VARIANCE REPORT")
    print(f"{'='*60}")
    print(f"\n{'Scenario':<35} {'Metric':<20} {'Mean':>10} {'StdDev':>10} {'Range':>10} {'Obs Delta':>12}")
    print("-" * 100)

    for scenario_key in args.scenarios:
        if scenario_key not in all_results["scenarios"]:
            continue
        variance = all_results["scenarios"][scenario_key]["variance"]
        for metric in FOCUS_METRICS:
            if metric not in variance:
                continue
            stats = variance[metric]
            obs = OBSERVED_DELTAS.get(scenario_key, {}).get(metric, "?")
            verdict = ""
            if isinstance(obs, (int, float)) and stats["range"] > 0:
                if abs(obs) < stats["range"]:
                    verdict = " << NOISE"
                elif abs(obs) < stats["range"] * 2:
                    verdict = " ~ borderline"
                else:
                    verdict = " >> REAL"
            print(f"{scenario_key:<35} {metric:<20} {stats['mean']:>10.1f} {stats['stddev']:>10.1f} {stats['range']:>10.1f} {str(obs):>12}{verdict}")

    print(f"\nResults saved to: {args.output}")


if __name__ == "__main__":
    main()
