// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include <gtest/gtest.h>

// See test_ros2_topic_name.cpp for why this whole file is guarded on
// WITH_ROS2: MakeExtensionHost() and ROS2.cpp (the real targets here) are only
// linked into libcarla_test_server, not libcarla_test_client, so calling them
// unconditionally would be an undefined reference on the client side. The
// CarlaRos2Extension.h ABI layout itself is pinned separately, without a guard,
// by test_ros2_extension_abi.cpp.
#if defined(WITH_ROS2)
#include "carla/ros2/extension/CarlaRos2Extension.h"
#include "carla/ros2/extension/ExtensionHost.h"
#include "carla/ros2/ROS2.h"

#include <string>

namespace {

// Observer bookkeeping: the callback runs synchronously on the dispatch thread
// (see CarlaRos2SensorObserver's contract in CarlaRos2Extension.h), so plain
// file-static capture is race-free within a single test.
int g_calls = 0;
CarlaRos2VehicleStatusView g_last_view = {};
std::string g_last_ros_name;
int g_last_sample_kind = 0;
uint32_t g_last_sample_actor_id = 0;

void obs(void* /*user*/, const CarlaRos2SensorSample* s) {
  ++g_calls;
  g_last_sample_kind = s->kind;
  g_last_sample_actor_id = s->actor_id;
  auto* v = static_cast<const CarlaRos2VehicleStatusView*>(s->data);
  g_last_view = *v;
  g_last_ros_name = v->ros_name ? v->ros_name : "";
}

}  // namespace

TEST(ros2_extension_host, vehicle_status_observer_is_called) {
  CarlaRos2Host host = carla::ros2::MakeExtensionHost();
  ASSERT_EQ(host.api_version, CARLA_ROS2_EXTENSION_API_VERSION);
  ASSERT_NE(host.host_ctx, nullptr);
  ASSERT_NE(host.register_sensor_observer, nullptr);
  // Start from a clean registry so a prior test's observer cannot leak in.
  carla::ros2::TeardownExtensionEndpoints();

  host.register_sensor_observer(
      host.host_ctx, CARLA_ROS2_SENSOR_VEHICLE_STATUS, obs, nullptr);
  g_calls = 0;
  carla::ros2::ROS2::GetInstance()->DispatchVehicleStatusObserversForTest(
      /*actor_id=*/7, "ego", /*velocity_mps=*/3.5, /*steer_rad=*/0.1,
      /*gear=*/2, /*sim_t=*/1.25);

  EXPECT_EQ(g_calls, 1);
  // The whole POD is pinned (not just velocity+kind) so the shared
  // DispatchVehicleStatusView fill path can never silently drop or reorder a
  // field between the live tap and this test driver.
  EXPECT_EQ(g_last_sample_kind, CARLA_ROS2_SENSOR_VEHICLE_STATUS);
  EXPECT_EQ(g_last_sample_actor_id, 7u);
  EXPECT_EQ(g_last_view.actor_id, 7u);
  EXPECT_EQ(g_last_ros_name, "ego");
  EXPECT_DOUBLE_EQ(g_last_view.velocity_mps, 3.5);
  EXPECT_DOUBLE_EQ(g_last_view.steering_tire_angle_rad, 0.1);
  EXPECT_EQ(g_last_view.gear, 2);
  EXPECT_DOUBLE_EQ(g_last_view.sim_time_s, 1.25);

  // Teardown must drop the extension-registered observer so a stale function
  // pointer into a soon-to-be-dlclose'd .so can never be dispatched into again
  // (the only lever Task 12's TeardownExtensionEndpoints owns; DDS endpoints
  // arrive in Task 13). After teardown, a second dispatch reaches nobody.
  carla::ros2::TeardownExtensionEndpoints();
  g_calls = 0;
  carla::ros2::ROS2::GetInstance()->DispatchVehicleStatusObserversForTest(
      /*actor_id=*/7, "ego", /*velocity_mps=*/9.0, /*steer_rad=*/0.0,
      /*gear=*/1, /*sim_t=*/2.0);
  EXPECT_EQ(g_calls, 0);
}

TEST(ros2_extension_host, duplicate_registration_is_ignored) {
  CarlaRos2Host host = carla::ros2::MakeExtensionHost();
  carla::ros2::TeardownExtensionEndpoints();

  // Same (kind, cb, user) triple registered twice must dispatch ONCE, not
  // twice — else a re-Load or a double register_observer would double the
  // per-frame sample rate into the extension.
  host.register_sensor_observer(
      host.host_ctx, CARLA_ROS2_SENSOR_VEHICLE_STATUS, obs, nullptr);
  host.register_sensor_observer(
      host.host_ctx, CARLA_ROS2_SENSOR_VEHICLE_STATUS, obs, nullptr);
  g_calls = 0;
  carla::ros2::ROS2::GetInstance()->DispatchVehicleStatusObserversForTest(
      /*actor_id=*/1, "ego", /*velocity_mps=*/1.0, /*steer_rad=*/0.0,
      /*gear=*/1, /*sim_t=*/0.0);
  EXPECT_EQ(g_calls, 1);

  carla::ros2::TeardownExtensionEndpoints();
}
#endif  // defined(WITH_ROS2)
