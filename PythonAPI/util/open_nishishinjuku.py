#!/usr/bin/env python3
"""Open the Nishi-Shinjuku map (AWSIM 3D + converter OpenDRIVE) in CARLA.

Loads the named CARLA map ``NishishinjukuMap`` via ``client.load_world`` so the
AWSIM 3D environment (buildings, ground) renders together with the OpenDRIVE
road network installed at the map's OpenDrive ``NishishinjukuMap.xodr``.
Moves the spectator to an overhead view and optionally saves a screenshot.

Prerequisite: a CARLA server with ``NishishinjukuMap`` cooked/available must be
running (e.g. the UE5 editor in Play-in-Editor mode).

Usage:
    python open_nishishinjuku.py [--map NishishinjukuMap] [--screenshot out.png]
"""

import argparse
import sys

MAP_NAME = "NishishinjukuMap"


def compute_overhead_view(points):
    """Return (x, y, z, pitch) for an overhead spectator framing all points.

    Args:
        points: iterable of (x, y) road-network coordinates.

    Returns:
        Tuple (center_x, center_y, height, pitch). ``height`` is scaled to the
        larger horizontal span (min 100 m); ``pitch`` is -90 (straight down).

    Raises:
        ValueError: if ``points`` is empty.
    """
    xs = [float(p[0]) for p in points]
    ys = [float(p[1]) for p in points]
    if not xs or not ys:
        raise ValueError("no points to frame")
    center_x = (min(xs) + max(xs)) / 2.0
    center_y = (min(ys) + max(ys)) / 2.0
    span = max(max(xs) - min(xs), max(ys) - min(ys))
    height = max(span * 0.6, 100.0)
    return center_x, center_y, height, -90.0


def _topology_points(topology, spawn_points):
    """Collect (x, y) points from a map's topology, or its spawn points."""
    points = []
    for wp_a, wp_b in topology:
        for waypoint in (wp_a, wp_b):
            loc = waypoint.transform.location
            points.append((loc.x, loc.y))
    if not points:
        points = [(sp.location.x, sp.location.y) for sp in spawn_points]
    return points


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", default=MAP_NAME, help="CARLA map name")
    parser.add_argument("--host", default="127.0.0.1", help="CARLA RPC host")
    parser.add_argument("--port", default=2000, type=int, help="CARLA RPC port")
    parser.add_argument("--screenshot", default=None,
                        help="Optional PNG path; saves an RGB camera frame")
    args = parser.parse_args()

    import carla  # imported here so the pure helpers stay importable for tests

    client = carla.Client(args.host, args.port)
    client.set_timeout(120.0)

    print("Loading map: %s ..." % args.map)
    world = client.load_world(args.map)

    carla_map = world.get_map()
    topology = carla_map.get_topology()
    spawn_points = carla_map.get_spawn_points()
    points = _topology_points(topology, spawn_points)
    if not points:
        print("ERROR: loaded world has no road topology", file=sys.stderr)
        sys.exit(1)

    center_x, center_y, height, pitch = compute_overhead_view(points)
    spectator = world.get_spectator()
    spectator.set_transform(
        carla.Transform(
            carla.Location(x=center_x, y=center_y, z=height),
            carla.Rotation(pitch=pitch),
        )
    )

    print("Loaded map: %s" % carla_map.name)
    print("Topology segments: %d, spawn points: %d"
          % (len(topology), len(spawn_points)))
    print("Spectator placed at (%.1f, %.1f, %.1f) looking straight down"
          % (center_x, center_y, height))

    if args.screenshot:
        _save_screenshot(world, center_x, center_y, height, args.screenshot)


def _save_screenshot(world, x, y, z, out_path):
    """Spawn a temporary overhead RGB camera and save one frame to out_path."""
    import carla
    blueprint = world.get_blueprint_library().find("sensor.camera.rgb")
    blueprint.set_attribute("image_size_x", "1920")
    blueprint.set_attribute("image_size_y", "1080")
    transform = carla.Transform(
        carla.Location(x=x, y=y, z=z),
        carla.Rotation(pitch=-90.0),
    )
    camera = world.spawn_actor(blueprint, transform)
    try:
        captured = {}
        camera.listen(lambda image: captured.setdefault("image", image))
        for _ in range(20):
            world.tick() if world.get_settings().synchronous_mode \
                else world.wait_for_tick()
            if "image" in captured:
                break
        if "image" in captured:
            captured["image"].save_to_disk(out_path)
            print("Saved screenshot to %s" % out_path)
        else:
            print("WARNING: no camera frame captured", file=sys.stderr)
    finally:
        camera.destroy()


if __name__ == "__main__":
    main()
