// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <array>
#include <cmath>

namespace carla {
namespace ros2 {

// Returns a yaw-only quaternion (w, x, y, z) corresponding to converting a
// magnetic compass heading (radians, clockwise from North) into ROS REP-103
// yaw (radians, counter-clockwise from East): yaw = pi/2 - compass.
//
// M_PI is used here, not std::numbers::pi_v, because this header is consumed
// by the carla-ros2-native ExternalProject in Ros2Native/, which does not
// configure CMAKE_CXX_STANDARD and therefore defaults to C++17 where
// <numbers> is unavailable.
inline std::array<float, 4> OrientationFromCompass(float compass) {
  const float yaw = static_cast<float>(M_PI) / 2.0f - compass;
  const float c = std::cos(yaw * 0.5f);
  const float s = std::sin(yaw * 0.5f);
  return {c, 0.0f, 0.0f, s};
}

// UE sensor-frame vector components (left-handed, X forward / Y right / Z up)
// -> ROS REP-103 sensor-frame components (right-handed, X forward / Y left /
// Z up). The axis map is diag(1, -1, 1) (Y flips); a POLAR vector (positions,
// velocities, specific force) converts through it directly.
inline std::array<float, 3> LinearUEToRos(float x, float y, float z) {
  return {x, -y, z};
}

// Same frame change for an ANGULAR velocity. Angular velocity is a
// PSEUDOVECTOR: under a handedness-changing axis map M it transforms as
// det(M) * M = -M, so X and Z flip instead of Y. Omitting this (the publisher
// historically copied UE components verbatim) inverted the fused yaw rate on
// a flip-mounted IMU and crashed closed-loop driving -- see test_imu_axes.cpp
// for the measured contract.
inline std::array<float, 3> AngularUEToRos(float x, float y, float z) {
  return {-x, y, -z};
}

}  // namespace ros2
}  // namespace carla
