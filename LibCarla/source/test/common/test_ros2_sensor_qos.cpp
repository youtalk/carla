// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include <gtest/gtest.h>

// See test_ros2_topic_name.cpp for why this whole file is guarded on
// WITH_ROS2: ROS2.cpp (the real target here) is only linked into
// libcarla_test_server, not libcarla_test_client, so calling ROS2::
// actor-registration APIs unconditionally would be an undefined reference on
// the client side.
#if defined(WITH_ROS2)
#include "carla/ros2/ROS2.h"
#include "carla/ros2/middleware/PublisherQos.h"

using namespace carla::ros2;

TEST(ros2_sensor_qos, best_effort_volatile_depth5_roundtrips) {
  auto ros2 = ROS2::GetInstance();
  int dummy = 0;
  PublisherQos qos;
  qos.reliability = ReliabilityKind::BestEffort;
  qos.durability = DurabilityKind::Volatile;
  qos.history_depth = 5u;
  ros2->RegisterSensor(&dummy, "lidar_top", "lidar_top", true,
                       "/sensing/lidar/top/pointcloud_raw_ex", qos);
  PublisherQos got = ros2->LookupSensorQosForTest(&dummy);
  EXPECT_EQ(got.reliability, ReliabilityKind::BestEffort);
  EXPECT_EQ(got.durability, DurabilityKind::Volatile);
  EXPECT_EQ(got.effective_history_depth(), 5u);
  ros2->UnregisterSensor(&dummy);
}

// Regression tripwire: every CarlaPointCloudPublisher subclass (lidar,
// semantic lidar, radar, DVS) hardcoded PublisherQos::SensorData()
// (best_effort/volatile/depth1) before per-sensor QoS support existed. A
// RegisterSensor call that omits qos entirely (the 5-arg overload) must
// reproduce that exact wire default — NOT the plain PublisherQos{} struct
// default (Reliable), which would silently let a slow ROS 2 subscriber block
// the publishing thread. This test fails against a RegisterSensor default of
// PublisherQos() (Reliable), which is exactly the regression it pins.
TEST(ros2_sensor_qos, default_is_best_effort_volatile_depth1) {
  auto ros2 = ROS2::GetInstance();
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "imu", "imu", true, "");   // 5-arg default qos
  PublisherQos got = ros2->LookupSensorQosForTest(&dummy);
  EXPECT_EQ(got.reliability, ReliabilityKind::BestEffort);
  EXPECT_EQ(got.durability, DurabilityKind::Volatile);
  EXPECT_EQ(got.effective_history_depth(), 1u);
  ros2->UnregisterSensor(&dummy);
}
#endif  // defined(WITH_ROS2)
