// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

// Pure-C-ABI guard: CarlaRos2Extension.h is DDS-free and STL-free by contract
// (see the header's own comment), so this test needs neither WITH_ROS2 nor
// the ROS2Native install path — it links into both libcarla_test_server and
// libcarla_test_client unconditionally.
#include <gtest/gtest.h>
#include "carla/ros2/extension/CarlaRos2Extension.h"

#include <cstddef>
#include <type_traits>

// ---------------------------------------------------------------------------
// Frozen v1 ABI layout pins (compile-time). These static_asserts lock the
// exact sizeof/offsetof of every struct that crosses the CarlaRos2Extension.h
// boundary, computed from the current (correct) layout. This header is
// vendored VERBATIM into the out-of-tree extension's own source tree, and the
// host/extension handshake is a single api_version integer with NO per-field
// forward-compat padding (see the header's own note) — so a field reorder,
// widen, or insert in ANY of these structs is an ABI break that MUST bump
// CARLA_ROS2_EXTENSION_API_VERSION, and MUST break this build first.
static_assert(sizeof(CarlaRos2Qos) == 8, "CarlaRos2Qos size changed");
static_assert(offsetof(CarlaRos2Qos, reliability) == 0, "CarlaRos2Qos::reliability moved");
static_assert(offsetof(CarlaRos2Qos, durability) == 1, "CarlaRos2Qos::durability moved");
static_assert(offsetof(CarlaRos2Qos, history_depth) == 4, "CarlaRos2Qos::history_depth moved");
static_assert(std::is_trivially_copyable<CarlaRos2Qos>::value, "CarlaRos2Qos must be trivially copyable");

static_assert(sizeof(CarlaRos2Transform) == 56, "CarlaRos2Transform size changed");
static_assert(offsetof(CarlaRos2Transform, x_cm) == 0, "CarlaRos2Transform::x_cm moved");
static_assert(offsetof(CarlaRos2Transform, y_cm) == 8, "CarlaRos2Transform::y_cm moved");
static_assert(offsetof(CarlaRos2Transform, z_cm) == 16, "CarlaRos2Transform::z_cm moved");
static_assert(offsetof(CarlaRos2Transform, qx) == 24, "CarlaRos2Transform::qx moved");
static_assert(offsetof(CarlaRos2Transform, qy) == 32, "CarlaRos2Transform::qy moved");
static_assert(offsetof(CarlaRos2Transform, qz) == 40, "CarlaRos2Transform::qz moved");
static_assert(offsetof(CarlaRos2Transform, qw) == 48, "CarlaRos2Transform::qw moved");
static_assert(std::is_trivially_copyable<CarlaRos2Transform>::value, "CarlaRos2Transform must be trivially copyable");

static_assert(sizeof(CarlaRos2VehicleStatusView) == 120, "CarlaRos2VehicleStatusView size changed");
static_assert(offsetof(CarlaRos2VehicleStatusView, actor_id) == 0, "CarlaRos2VehicleStatusView::actor_id moved");
static_assert(offsetof(CarlaRos2VehicleStatusView, ros_name) == 8, "CarlaRos2VehicleStatusView::ros_name moved");
static_assert(offsetof(CarlaRos2VehicleStatusView, transform) == 16, "CarlaRos2VehicleStatusView::transform moved");
static_assert(offsetof(CarlaRos2VehicleStatusView, velocity_mps) == 72, "CarlaRos2VehicleStatusView::velocity_mps moved");
static_assert(offsetof(CarlaRos2VehicleStatusView, lateral_velocity_mps) == 80, "CarlaRos2VehicleStatusView::lateral_velocity_mps moved");
static_assert(offsetof(CarlaRos2VehicleStatusView, yaw_rate_rps) == 88, "CarlaRos2VehicleStatusView::yaw_rate_rps moved");
static_assert(offsetof(CarlaRos2VehicleStatusView, steering_tire_angle_rad) == 96, "CarlaRos2VehicleStatusView::steering_tire_angle_rad moved");
static_assert(offsetof(CarlaRos2VehicleStatusView, gear) == 104, "CarlaRos2VehicleStatusView::gear moved");
static_assert(offsetof(CarlaRos2VehicleStatusView, sim_time_s) == 112, "CarlaRos2VehicleStatusView::sim_time_s moved");
static_assert(std::is_trivially_copyable<CarlaRos2VehicleStatusView>::value, "CarlaRos2VehicleStatusView must be trivially copyable");

static_assert(sizeof(CarlaRos2AckermannPod) == 20, "CarlaRos2AckermannPod size changed");
static_assert(offsetof(CarlaRos2AckermannPod, steer) == 0, "CarlaRos2AckermannPod::steer moved");
static_assert(offsetof(CarlaRos2AckermannPod, steer_speed) == 4, "CarlaRos2AckermannPod::steer_speed moved");
static_assert(offsetof(CarlaRos2AckermannPod, speed) == 8, "CarlaRos2AckermannPod::speed moved");
static_assert(offsetof(CarlaRos2AckermannPod, acceleration) == 12, "CarlaRos2AckermannPod::acceleration moved");
static_assert(offsetof(CarlaRos2AckermannPod, jerk) == 16, "CarlaRos2AckermannPod::jerk moved");
static_assert(std::is_trivially_copyable<CarlaRos2AckermannPod>::value, "CarlaRos2AckermannPod must be trivially copyable");

static_assert(sizeof(CarlaRos2SensorSample) == 32, "CarlaRos2SensorSample size changed");
static_assert(offsetof(CarlaRos2SensorSample, kind) == 0, "CarlaRos2SensorSample::kind moved");
static_assert(offsetof(CarlaRos2SensorSample, actor_id) == 4, "CarlaRos2SensorSample::actor_id moved");
static_assert(offsetof(CarlaRos2SensorSample, ros_name) == 8, "CarlaRos2SensorSample::ros_name moved");
static_assert(offsetof(CarlaRos2SensorSample, data) == 16, "CarlaRos2SensorSample::data moved");
static_assert(offsetof(CarlaRos2SensorSample, data_size) == 24, "CarlaRos2SensorSample::data_size moved");
static_assert(std::is_trivially_copyable<CarlaRos2SensorSample>::value, "CarlaRos2SensorSample must be trivially copyable");

// Host/Extension are vtable structs (function pointers + context); pinned by
// sizeof + trivial-copyability only, no offsetof — their fields are not laid
// out over the wire, but a size change still signals an ABI-relevant edit
// (added/removed/reordered vtable slot) that must bump the version.
static_assert(sizeof(CarlaRos2Host) == 72, "CarlaRos2Host size changed");
static_assert(std::is_trivially_copyable<CarlaRos2Host>::value, "CarlaRos2Host must be trivially copyable");

static_assert(sizeof(CarlaRos2Extension) == 32, "CarlaRos2Extension size changed");
static_assert(std::is_trivially_copyable<CarlaRos2Extension>::value, "CarlaRos2Extension must be trivially copyable");

TEST(ros2_extension_abi, version_is_one) {
  EXPECT_EQ(CARLA_ROS2_EXTENSION_API_VERSION, 1u);
}
TEST(ros2_extension_abi, host_and_extension_are_standard_layout) {
  // A stable C ABI requires standard-layout, trivially copyable vtable structs.
  EXPECT_TRUE(std::is_standard_layout<CarlaRos2Host>::value);
  EXPECT_TRUE(std::is_standard_layout<CarlaRos2Extension>::value);
  EXPECT_TRUE(std::is_standard_layout<CarlaRos2VehicleStatusView>::value);
}
TEST(ros2_extension_abi, sensor_kind_enum_values_are_pinned) {
  EXPECT_EQ(CARLA_ROS2_SENSOR_VEHICLE_STATUS, 5);
  EXPECT_EQ(CARLA_ROS2_SENSOR_LIDAR_EXT, 2);
}
