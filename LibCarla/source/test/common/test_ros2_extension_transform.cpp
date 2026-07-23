// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include <gtest/gtest.h>

#include <cmath>

// MakeExtensionTransformMetresDeg is a pure, header-only translation helper (like the
// ue_*_to_ros_* family and the extension-side MgrsOffset.h): it takes a server-side ego
// pose in the CARLA client frame (METRES + degrees) and produces the CarlaRos2Transform an
// extension VEHICLE_STATUS observer receives. It is NOT guarded on WITH_ROS2 (same as
// test_ros2_extension_abi.cpp): the helper only needs the pure-C ABI struct, no ROS2.cpp
// symbols, so it links into both libcarla_test_client and libcarla_test_server.
#include "carla/ros2/extension/ExtensionTransform.h"

using carla::ros2::MakeExtensionTransformMetresDeg;

// M4-blocker #3 (docs/phase-b-report.md): the host filled CarlaRos2Transform.x_cm with the
// ego location in METRES, but the ABI (CarlaRos2Extension.h) documents those fields as
// CENTIMETRES and the extension divides by 100 -- so the synthesised GNSS pose landed ~350 m
// (a consistent /100 scale error) off the true ego. The fix scales metres->cm at the host
// boundary. This test pins that: a location of N metres must arrive as N*100 centimetres.
TEST(ros2_extension_transform, location_metres_are_scaled_to_centimetres) {
  // Nishi-Shinjuku spawn-point-0 ego (docs/phase-b-report.md worked example), in metres.
  const CarlaRos2Transform t =
      MakeExtensionTransformMetresDeg(-278.39, 220.54, -1.26, 0.0, 0.0, 0.0);
  EXPECT_DOUBLE_EQ(t.x_cm, -27839.0);
  EXPECT_DOUBLE_EQ(t.y_cm, 22054.0);
  EXPECT_DOUBLE_EQ(t.z_cm, -126.0);
}

TEST(ros2_extension_transform, origin_is_zero) {
  const CarlaRos2Transform t = MakeExtensionTransformMetresDeg(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  EXPECT_DOUBLE_EQ(t.x_cm, 0.0);
  EXPECT_DOUBLE_EQ(t.y_cm, 0.0);
  EXPECT_DOUBLE_EQ(t.z_cm, 0.0);
}

// Orientation regression guard on the (verbatim-moved) CARLA-frame Euler->quaternion:
// identity rotation -> identity quaternion, and a pure +90 deg yaw -> (0, 0, sin45, cos45).
// No handedness flip happens here -- the extension owns its own Y-flip (MgrsOffset.h).
TEST(ros2_extension_transform, identity_rotation_is_identity_quaternion) {
  const CarlaRos2Transform t = MakeExtensionTransformMetresDeg(0, 0, 0, 0.0, 0.0, 0.0);
  EXPECT_DOUBLE_EQ(t.qx, 0.0);
  EXPECT_DOUBLE_EQ(t.qy, 0.0);
  EXPECT_DOUBLE_EQ(t.qz, 0.0);
  EXPECT_DOUBLE_EQ(t.qw, 1.0);
}

TEST(ros2_extension_transform, pure_yaw_quaternion) {
  const CarlaRos2Transform t = MakeExtensionTransformMetresDeg(0, 0, 0, 0.0, 0.0, 90.0);
  const double s = std::sin(M_PI / 4.0);
  EXPECT_NEAR(t.qx, 0.0, 1e-12);
  EXPECT_NEAR(t.qy, 0.0, 1e-12);
  EXPECT_NEAR(t.qz, s, 1e-12);
  EXPECT_NEAR(t.qw, s, 1e-12);
}
