# ROS2 Native Example

This example demonstrates how to utilize the ROS 2 native interface in CARLA: it spawns a sensorized hero vehicle on autopilot and visualizes its sensor output in RViz.

## The latched map topic

The native ROS 2 interface publishes a latched `std_msgs/String` with the full OpenDRIVE description of the current map on `/carla/map` (transient local durability, re-published on every map load). `std_msgs/String` carries no `Header`, matching the carla-ros-bridge `/carla/map` topic: the latched sample has no stamp or episode id, but it always describes the currently loaded map because the single cached sample is overwritten on every map load. Any node can join late, read the map once, and combine it with the live sensor streams.

Note that this build does not broadcast a vehicle TF tree (the native `map->odom`/`odom->hero` transforms are intentionally excluded), so RViz has no transform source and displays render each stream in its own frame rather than composing them into a single `map`-anchored view.

## Files

| File | Role |
|------|------|
| `ros2_native.py` | Spawns the hero vehicle defined in `stack.json` (camera, lidar, GNSS, IMU) and drives it on autopilot. Destroys the actors and restores the world settings on shutdown. |
| `stack.json` | Sensor setup of the hero vehicle. Edit it to adjust the sensors. |
| `run_rviz.sh` | Runs RViz in Docker with the bundled preset. |
| `rviz/ros2_native.rviz` | RViz preset: camera panel, lidar point cloud, a TF tree display and a `/carla/map_markers` marker display (the latter two are present in the preset but unpopulated on this branch: no TF is broadcast, and nothing publishes the markers). |
| `Dockerfile`, `config/` | Base RViz image and the RMW configuration files mounted into the containers. |

## Prerequisites

To run this example, ensure `docker` is installed in your system, which is used to launch an instance of `rviz` for visualizing the data.

## Usage

### Step 1: Start the CARLA Simulator with ROS2 enabled

```bash
./CarlaUnreal.sh --ros2
```

To select the middleware, add the `--rmw=` argument (`fastdds` by default, or
`cyclonedds` or `zenoh` on Linux):

```bash
./CarlaUnreal.sh --ros2 --rmw=cyclonedds
```

> [!NOTE]
> To use the Zenoh middleware, add `--rmw=zenoh` to the launch command above and to the scripts below, and start a Zenoh router first:
> ```bash
> docker run --rm --net=host carla-rviz-<distro>-zenoh ros2 run rmw_zenoh_cpp rmw_zenohd
> ```
> The `carla-rviz-<distro>-zenoh` image is built the first time you run `run_rviz.sh` (`<distro>` is `humble` or `jazzy`).

### Step 2: Run the ROS2 Example

Execute the ROS 2 example script (it only needs the `carla` Python package, no ROS environment):

```bash
python3 ros2_native.py --file stack.json
```

* The `stack.json` file defines the sensor configuration.
* You can edit this file to adjust the sensor setup according to your requirements.

### Step 3: Run RViz to visualize the result

```bash
./run_rviz.sh
```

`run_rviz.sh` accepts `--distro=<humble|jazzy>` and `--rmw=<fastdds|cyclonedds|zenoh>`; the `--rmw` value must match the middleware the simulator was launched with (e.g. `./run_rviz.sh --distro=humble --rmw=cyclonedds`).

With the bundled preset you get the lidar point cloud rendered in its own sensor frame (not composed into the `map` frame, since this build does not broadcast a TF tree — see above) and the camera image panel.

Tip: if RViz started before a latched topic (e.g. `/carla/map`) was available it may have dropped the sample. Toggle the display checkbox to force a resubscription.

### Optional: Custom ROS 2 domain id

By default CARLA and `rviz` communicate on the default ROS 2 domain. To isolate the
session on a specific domain, launch the server with `--ros-domain-id=<N>` and pass the
same value to `run_rviz.sh`:

```bash
# Server:
./CarlaUnreal.sh --ros2 --ros-domain-id=42

# RViz on the matching domain:
./run_rviz.sh --ros-domain-id=42
```

The domain id must be an integer in the range 0-232 and must match on both sides for the
topics to be discovered.

If you omit `--ros-domain-id`, the server falls back to the standard `ROS_DOMAIN_ID`
environment variable, and then to the default domain 0. For example, exporting the variable
before launching applies the same domain without the option:

```bash
export ROS_DOMAIN_ID=42
./CarlaUnreal.sh --ros2          # server uses domain 42
./run_rviz.sh --ros-domain-id=42
```

When both are set, `--ros-domain-id` takes precedence over `ROS_DOMAIN_ID`.
