#!/usr/bin/env python3
"""
CARLA Performance Benchmark Suite

Measures CPU/GPU usage, RAM/VRAM consumption, and FPS across multiple scenarios.
Designed for headless (offscreen) server comparison between UE builds.

Usage:
    python3 benchmark_performance.py [--host H] [--port P] [--output results.json]
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime

import carla
import psutil


def get_gpu_metrics():
    """Query NVIDIA GPU metrics via nvidia-smi."""
    try:
        result = subprocess.run(
            [
                "nvidia-smi",
                "--query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu,power.draw",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            timeout=5,
        )
        if result.returncode != 0:
            return None
        line = result.stdout.strip().split("\n")[0]
        vals = [v.strip() for v in line.split(",")]
        return {
            "gpu_util_pct": float(vals[0]),
            "vram_used_mb": float(vals[1]),
            "vram_total_mb": float(vals[2]),
            "gpu_temp_c": float(vals[3]),
            "power_draw_w": float(vals[4]),
        }
    except Exception:
        return None


def get_cpu_memory_metrics(server_pid=None, server_proc=None):
    """Get system-wide CPU/memory and optionally per-process metrics."""
    mem = psutil.virtual_memory()
    cpu_pct = psutil.cpu_percent(interval=None)
    per_cpu = psutil.cpu_percent(interval=None, percpu=True)

    metrics = {
        "cpu_util_pct": cpu_pct,
        "cpu_util_max_core_pct": max(per_cpu) if per_cpu else 0.0,
        "ram_used_mb": mem.used / (1024 * 1024),
        "ram_total_mb": mem.total / (1024 * 1024),
        "ram_available_mb": mem.available / (1024 * 1024),
    }

    if server_proc:
        try:
            proc_mem = server_proc.memory_info()
            metrics["server_rss_mb"] = proc_mem.rss / (1024 * 1024)
            metrics["server_vms_mb"] = proc_mem.vms / (1024 * 1024)
            metrics["server_cpu_pct"] = server_proc.cpu_percent(interval=None)
            metrics["server_threads"] = server_proc.num_threads()
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    elif server_pid:
        try:
            proc = psutil.Process(server_pid)
            proc_mem = proc.memory_info()
            metrics["server_rss_mb"] = proc_mem.rss / (1024 * 1024)
            metrics["server_vms_mb"] = proc_mem.vms / (1024 * 1024)
            metrics["server_cpu_pct"] = proc.cpu_percent(interval=None)
            metrics["server_threads"] = proc.num_threads()
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass

    return metrics


def find_carla_server_pid():
    """Find the PID of the running CARLA server process (the actual binary, not shell wrapper)."""
    candidates = []
    for proc in psutil.process_iter(["pid", "name", "cmdline", "memory_info"]):
        try:
            name = proc.info["name"] or ""
            cmdline = " ".join(proc.info["cmdline"] or [])
            # Match the actual binary, not shell wrappers
            if "CarlaUnreal-Linux-Shipping" in name or "CarlaUnreal-Linux-Shipping" in cmdline:
                rss = proc.info["memory_info"].rss if proc.info["memory_info"] else 0
                candidates.append((proc.info["pid"], rss))
            elif "CarlaUnreal" in name and proc.info["memory_info"]:
                rss = proc.info["memory_info"].rss
                if rss > 100 * 1024 * 1024:  # >100MB = likely the real process
                    candidates.append((proc.info["pid"], rss))
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    if candidates:
        # Return the one with the largest RSS (the actual UE process)
        candidates.sort(key=lambda x: x[1], reverse=True)
        return candidates[0][0]
    return None


def collect_samples(duration_sec, interval_sec, server_pid, world):
    """Collect resource usage samples over a period."""
    samples = []
    end_time = time.time() + duration_sec
    # Prime CPU percent counters
    psutil.cpu_percent(interval=None)
    if server_pid:
        try:
            psutil.Process(server_pid).cpu_percent(interval=None)
        except Exception:
            pass

    while time.time() < end_time:
        sample = {"timestamp": time.time()}

        gpu = get_gpu_metrics()
        if gpu:
            sample.update(gpu)

        cpu_mem = get_cpu_memory_metrics(server_pid)
        sample.update(cpu_mem)

        try:
            snapshot = world.get_snapshot()
            sample["server_fps"] = snapshot.timestamp.delta_seconds
            if sample["server_fps"] > 0:
                sample["server_fps"] = 1.0 / sample["server_fps"]
        except Exception:
            pass

        samples.append(sample)
        time.sleep(interval_sec)

    return samples


def compute_stats(samples, key):
    """Compute min/max/mean/median for a metric across samples."""
    values = [s[key] for s in samples if key in s]
    if not values:
        return None
    values.sort()
    n = len(values)
    return {
        "min": round(values[0], 2),
        "max": round(values[-1], 2),
        "mean": round(sum(values) / n, 2),
        "median": round(values[n // 2], 2),
        "samples": n,
    }


def summarize_samples(samples):
    """Create a statistical summary from collected samples."""
    keys = [
        "gpu_util_pct",
        "vram_used_mb",
        "gpu_temp_c",
        "power_draw_w",
        "cpu_util_pct",
        "cpu_util_max_core_pct",
        "ram_used_mb",
        "ram_available_mb",
        "server_rss_mb",
        "server_vms_mb",
        "server_cpu_pct",
        "server_threads",
        "server_fps",
    ]
    summary = {}
    for key in keys:
        stats = compute_stats(samples, key)
        if stats:
            summary[key] = stats
    return summary


def spawn_vehicles(client, world, count):
    """Spawn vehicles with autopilot."""
    blueprints = world.get_blueprint_library().filter("vehicle.*")
    blueprints = [b for b in blueprints if b.get_attribute("base_type") == "car"]
    spawn_points = world.get_map().get_spawn_points()

    vehicles = []
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
    for r in results:
        if not r.error:
            vehicles.append(r.actor_id)

    print(f"  Spawned {len(vehicles)}/{count} vehicles")
    return vehicles


def spawn_walkers(client, world, count):
    """Spawn pedestrian walkers."""
    blueprints = world.get_blueprint_library().filter("walker.pedestrian.*")
    spawn_points = []
    for _ in range(count):
        loc = world.get_random_location_from_navigation()
        if loc:
            spawn_points.append(carla.Transform(loc))

    walkers = []
    batch = [carla.command.SpawnActor(blueprints[i % len(blueprints)], sp) for i, sp in enumerate(spawn_points)]
    results = client.apply_batch_sync(batch, True)
    for r in results:
        if not r.error:
            walkers.append(r.actor_id)

    # Spawn walker controllers
    controller_bp = world.get_blueprint_library().find("controller.ai.walker")
    controller_batch = []
    for wid in walkers:
        controller_batch.append(
            carla.command.SpawnActor(controller_bp, carla.Transform(), wid)
        )
    controller_results = client.apply_batch_sync(controller_batch, True)
    controllers = []
    for r in controller_results:
        if not r.error:
            controllers.append(r.actor_id)

    # Start walking
    world.tick()
    for cid in controllers:
        try:
            controller = world.get_actor(cid)
            if controller:
                controller.start()
                controller.go_to_location(world.get_random_location_from_navigation())
                controller.set_max_speed(1.5)
        except Exception:
            pass

    print(f"  Spawned {len(walkers)}/{count} walkers")
    return walkers + controllers


def attach_sensors(world, vehicle_id):
    """Attach common sensors to a vehicle."""
    vehicle = world.get_actor(vehicle_id)
    if not vehicle:
        return []

    sensors = []
    bp_lib = world.get_blueprint_library()

    sensor_configs = [
        ("sensor.camera.rgb", {"image_size_x": "1920", "image_size_y": "1080", "fov": "90"}, carla.Transform(carla.Location(x=1.5, z=2.4))),
        ("sensor.camera.depth", {"image_size_x": "1920", "image_size_y": "1080", "fov": "90"}, carla.Transform(carla.Location(x=1.5, z=2.4))),
        ("sensor.lidar.ray_cast", {"range": "50", "channels": "64", "points_per_second": "1200000", "rotation_frequency": "20"}, carla.Transform(carla.Location(z=2.5))),
        ("sensor.other.radar", {}, carla.Transform(carla.Location(x=2.0, z=1.0))),
    ]

    for sensor_type, attrs, transform in sensor_configs:
        bp = bp_lib.find(sensor_type)
        if not bp:
            continue
        for k, v in attrs.items():
            if bp.has_attribute(k):
                bp.set_attribute(k, v)
        sensor = world.spawn_actor(bp, transform, attach_to=vehicle)
        # Attach a dummy listener to consume data
        sensor.listen(lambda data: None)
        sensors.append(sensor.id)
        print(f"  Attached {sensor_type}")

    return sensors


def destroy_actors(client, actor_ids):
    """Destroy a list of actors."""
    if not actor_ids:
        return
    batch = [carla.command.DestroyActor(aid) for aid in actor_ids]
    client.apply_batch_sync(batch, True)


def run_scenario(name, client, world, server_pid, setup_fn, duration=30, warmup=5, sample_interval=0.5):
    """Run a single benchmark scenario."""
    print(f"\n=== Scenario: {name} ===")
    actor_ids = []

    # Create persistent Process object for server
    server_proc = None
    if server_pid:
        try:
            server_proc = psutil.Process(server_pid)
        except psutil.NoSuchProcess:
            pass

    try:
        actor_ids = setup_fn(client, world)

        # Warmup: let simulation stabilize
        print(f"  Warming up for {warmup}s...")
        for _ in range(int(warmup / 0.05)):
            world.tick()

        # Collect samples
        print(f"  Collecting samples for {duration}s...")
        samples = []
        end_time = time.time() + duration

        # Prime CPU counters (first call initializes, subsequent calls return delta)
        psutil.cpu_percent(interval=None)
        psutil.cpu_percent(interval=None, percpu=True)
        if server_proc:
            try:
                server_proc.cpu_percent(interval=None)
            except Exception:
                pass
        # Let at least one tick pass so the first sample has a valid delta
        world.tick()
        time.sleep(0.1)

        tick_count = 1
        while time.time() < end_time:
            world.tick()
            tick_count += 1

            # Sample every N ticks
            if tick_count % max(1, int(sample_interval / 0.05)) == 0:
                sample = {"timestamp": time.time()}

                gpu = get_gpu_metrics()
                if gpu:
                    sample.update(gpu)

                cpu_mem = get_cpu_memory_metrics(server_pid, server_proc)
                sample.update(cpu_mem)

                try:
                    snapshot = world.get_snapshot()
                    dt = snapshot.timestamp.delta_seconds
                    if dt > 0:
                        sample["server_fps"] = 1.0 / dt
                except Exception:
                    pass

                samples.append(sample)

        summary = summarize_samples(samples)
        summary["tick_count"] = tick_count
        summary["duration_sec"] = duration
        summary["effective_tps"] = round(tick_count / duration, 2)

        print(f"  Completed: {tick_count} ticks, {len(samples)} samples")
        for key in ["gpu_util_pct", "vram_used_mb", "cpu_util_pct", "ram_used_mb", "server_rss_mb", "server_cpu_pct", "server_fps"]:
            if key in summary:
                print(f"    {key}: mean={summary[key]['mean']}, min={summary[key]['min']}, max={summary[key]['max']}")

        return summary

    finally:
        if actor_ids:
            print(f"  Cleaning up {len(actor_ids)} actors...")
            destroy_actors(client, actor_ids)
            # Let cleanup propagate
            for _ in range(20):
                world.tick()


def main():
    parser = argparse.ArgumentParser(description="CARLA Performance Benchmark")
    parser.add_argument("--host", default="127.0.0.1", help="CARLA host")
    parser.add_argument("--port", default=2000, type=int, help="CARLA port")
    parser.add_argument("--output", default="benchmark_results.json", help="Output JSON file")
    parser.add_argument("--duration", default=30, type=int, help="Duration per scenario in seconds")
    parser.add_argument("--map", default="Town03_Opt", help="Map to use for benchmarks")
    parser.add_argument("--label", default="", help="Label for this benchmark run (e.g., 'pre-patch', 'post-patch')")
    args = parser.parse_args()

    print(f"CARLA Performance Benchmark")
    print(f"  Host: {args.host}:{args.port}")
    print(f"  Duration per scenario: {args.duration}s")
    print(f"  Label: {args.label or '(none)'}")
    print()

    client = carla.Client(args.host, args.port)
    client.set_timeout(30.0)

    # Load the target map
    print(f"Loading map: {args.map}")
    world = client.load_world(args.map)
    time.sleep(5)

    # Set synchronous mode
    settings = world.get_settings()
    settings.synchronous_mode = True
    settings.fixed_delta_seconds = 0.05  # 20 FPS simulation
    settings.no_rendering_mode = False
    world.apply_settings(settings)

    tm = client.get_trafficmanager()
    tm.set_synchronous_mode(True)

    server_pid = find_carla_server_pid()
    if server_pid:
        try:
            proc = psutil.Process(server_pid)
            proc_mem = proc.memory_info()
            print(f"  Server PID: {server_pid}")
            print(f"  Server process: {proc.name()} (RSS: {proc_mem.rss / (1024*1024):.0f} MB, threads: {proc.num_threads()})")
        except Exception:
            print(f"  Server PID: {server_pid} (could not read details)")
    else:
        print("  Server PID: not found (server_rss/server_cpu will be unavailable)")

    # System info
    gpu_info = get_gpu_metrics()
    sys_info = {
        "timestamp": datetime.now().isoformat(),
        "label": args.label,
        "map": args.map,
        "cpu_count": psutil.cpu_count(),
        "cpu_count_physical": psutil.cpu_count(logical=False),
        "ram_total_mb": round(psutil.virtual_memory().total / (1024 * 1024), 0),
        "gpu_name": None,
        "vram_total_mb": gpu_info["vram_total_mb"] if gpu_info else None,
    }
    try:
        result = subprocess.run(
            ["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"],
            capture_output=True, text=True, timeout=5,
        )
        sys_info["gpu_name"] = result.stdout.strip()
    except Exception:
        pass

    print(f"  GPU: {sys_info['gpu_name']}")
    print(f"  VRAM: {sys_info['vram_total_mb']} MB")
    print(f"  RAM: {sys_info['ram_total_mb']} MB")
    print(f"  CPU cores: {sys_info['cpu_count_physical']} physical, {sys_info['cpu_count']} logical")

    results = {"system": sys_info, "scenarios": {}}

    # --- Scenario 1: Idle (empty world, no traffic) ---
    def setup_idle(client, world):
        return []

    results["scenarios"]["01_idle"] = run_scenario(
        "Idle (no actors)", client, world, server_pid, setup_idle, duration=args.duration
    )

    # --- Scenario 2: Light traffic (20 vehicles) ---
    def setup_light_traffic(client, world):
        return spawn_vehicles(client, world, 20)

    results["scenarios"]["02_light_traffic_20v"] = run_scenario(
        "Light Traffic (20 vehicles)", client, world, server_pid, setup_light_traffic, duration=args.duration
    )

    # --- Scenario 3: Heavy traffic (50 vehicles + 30 walkers) ---
    def setup_heavy_traffic(client, world):
        vehicles = spawn_vehicles(client, world, 50)
        walkers = spawn_walkers(client, world, 30)
        return vehicles + walkers

    results["scenarios"]["03_heavy_traffic_50v_30w"] = run_scenario(
        "Heavy Traffic (50 vehicles + 30 walkers)", client, world, server_pid, setup_heavy_traffic, duration=args.duration
    )

    # --- Scenario 4: Sensors (1 vehicle + RGB + Depth + LiDAR + Radar) ---
    def setup_sensors(client, world):
        vehicles = spawn_vehicles(client, world, 1)
        if not vehicles:
            return []
        sensors = attach_sensors(world, vehicles[0])
        return vehicles + sensors

    results["scenarios"]["04_sensors_single_vehicle"] = run_scenario(
        "Sensors (1 vehicle + RGB + Depth + LiDAR + Radar)", client, world, server_pid, setup_sensors, duration=args.duration
    )

    # --- Scenario 5: Combined (30 vehicles + 20 walkers + sensors on ego) ---
    def setup_combined(client, world):
        vehicles = spawn_vehicles(client, world, 30)
        walkers = spawn_walkers(client, world, 20)
        sensors = []
        if vehicles:
            sensors = attach_sensors(world, vehicles[0])
        return vehicles + walkers + sensors

    results["scenarios"]["05_combined_30v_20w_sensors"] = run_scenario(
        "Combined (30 vehicles + 20 walkers + sensors)", client, world, server_pid, setup_combined, duration=args.duration
    )

    # --- Scenario 6: Rendering stress (weather + time of day) ---
    def setup_rendering_stress(client, world):
        weather = carla.WeatherParameters(
            cloudiness=90.0,
            precipitation=80.0,
            precipitation_deposits=80.0,
            wind_intensity=80.0,
            sun_altitude_angle=-30.0,  # Night
            fog_density=50.0,
            fog_distance=30.0,
            wetness=100.0,
        )
        world.set_weather(weather)
        vehicles = spawn_vehicles(client, world, 30)
        sensors = []
        if vehicles:
            sensors = attach_sensors(world, vehicles[0])
        return vehicles + sensors

    results["scenarios"]["06_rendering_stress_night_rain"] = run_scenario(
        "Rendering Stress (night + rain + fog + 30 vehicles + sensors)", client, world, server_pid, setup_rendering_stress, duration=args.duration
    )

    # Reset weather
    world.set_weather(carla.WeatherParameters.ClearNoon)

    # Restore async mode
    settings = world.get_settings()
    settings.synchronous_mode = False
    settings.fixed_delta_seconds = None
    world.apply_settings(settings)

    # Save results
    output_path = args.output
    with open(output_path, "w") as f:
        json.dump(results, f, indent=2)

    print(f"\n{'='*60}")
    print(f"Results saved to: {output_path}")
    print(f"{'='*60}")

    # Print summary table
    print(f"\n{'Scenario':<45} {'GPU%':>6} {'VRAM MB':>9} {'CPU%':>6} {'RAM MB':>9} {'RSS MB':>9} {'FPS':>6}")
    print("-" * 95)
    for name, data in results["scenarios"].items():
        gpu = data.get("gpu_util_pct", {}).get("mean", "-")
        vram = data.get("vram_used_mb", {}).get("mean", "-")
        cpu = data.get("cpu_util_pct", {}).get("mean", "-")
        ram = data.get("ram_used_mb", {}).get("mean", "-")
        rss = data.get("server_rss_mb", {}).get("mean", "-")
        fps = data.get("server_fps", {}).get("mean", "-")
        print(f"{name:<45} {gpu:>6} {vram:>9} {cpu:>6} {ram:>9} {rss:>9} {fps:>6}")


if __name__ == "__main__":
    main()
