// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <cmath>

#include "carla/ros2/extension/CarlaRos2Extension.h"

namespace carla {
namespace ros2 {

// Build the CarlaRos2Transform delivered to an extension VEHICLE_STATUS observer from a
// server-side ego pose expressed in the CARLA client frame (metres + degrees).
//
// UNITS: carla::geom::Transform.location is METRES
// (the client frame -- the odometry publisher beside the call site writes it straight into
// a ROS metre pose), but the ABI documents x_cm/y_cm/z_cm as CENTIMETRES
// (CarlaRos2Extension.h) and the extension divides by 100 to recover metres for MGRS
// synthesis. Passing metres straight through therefore landed the synthesised GNSS pose
// ~350 m off the true ego (a consistent /100 scale error). We scale metres->cm HERE, at the
// host boundary, so the field honours its documented unit and the extension stays unchanged.
//
// Orientation is the CARLA-frame Euler(roll, pitch, yaw)->quaternion (intrinsic Z-Y-X /
// aircraft order), with NO handedness flip: the extension owns its own Y-flip
// (extension/.../geo/MgrsOffset.h). Kept pure + header-only so the unit conversion and the
// quaternion are pinned by a gtest (test_ros2_extension_transform.cpp) rather than buried in
// ProcessDataFromVehicle. M_PI is the same IEEE-754 double as carla::geom::Math::Pi<double>().
inline CarlaRos2Transform MakeExtensionTransformMetresDeg(
    double x_m, double y_m, double z_m,
    double roll_deg, double pitch_deg, double yaw_deg) {
  CarlaRos2Transform transform = {};
  transform.x_cm = x_m * 100.0;
  transform.y_cm = y_m * 100.0;
  transform.z_cm = z_m * 100.0;
  const double half_pitch = pitch_deg * M_PI / 360.0;
  const double half_yaw = yaw_deg * M_PI / 360.0;
  const double half_roll = roll_deg * M_PI / 360.0;
  const double cp = std::cos(half_pitch), sp = std::sin(half_pitch);
  const double cy = std::cos(half_yaw), sy = std::sin(half_yaw);
  const double cr = std::cos(half_roll), sr = std::sin(half_roll);
  transform.qw = cr * cp * cy + sr * sp * sy;
  transform.qx = sr * cp * cy - cr * sp * sy;
  transform.qy = cr * sp * cy + sr * cp * sy;
  transform.qz = cr * cp * sy - sr * sp * cy;
  return transform;
}

}  // namespace ros2
}  // namespace carla
