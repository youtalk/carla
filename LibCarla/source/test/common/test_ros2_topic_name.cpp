// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include <gtest/gtest.h>

// carla/ros2/*.cpp (this file's real target, ROS2.cpp) is only compiled into
// carla-server (see LibCarla/CMakeLists.txt's ENABLE_ROS2 glob), which is what
// libcarla_test_server links. test/common/*.cpp is also compiled into
// libcarla_test_client (linked against carla-client), which never builds
// ROS2.cpp, so calling ROS2:: actor-registration APIs there is an undefined
// reference at link time. WITH_ROS2 is carla-server's own PUBLIC compile
// definition (propagates to libcarla_test_server, not libcarla_test_client),
// so guarding on it — the same macro the Unreal plugin uses for this exact
// distinction (see ActorDispatcher.cpp) — keeps this test file a no-op TU on
// the client side instead of a link failure.
#if defined(WITH_ROS2)
#include "carla/ros2/ROS2.h"
#include "carla/ros2/publishers/CarlaRadarPublisher.h"
#include "carla/ros2/publishers/CarlaSemanticLidarPublisher.h"
#include "carla/ros2/publishers/PointCloudTopic.h"

#include <memory>

using carla::ros2::ROS2;
using carla::ros2::CarlaRadarPublisher;
using carla::ros2::CarlaSemanticLidarPublisher;
using carla::ros2::ComposePointCloudTopic;

TEST(ros2_topic_name, empty_override_uses_default_composition) {
  auto ros2 = ROS2::GetInstance();
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "lidar_top", "lidar_top", true, "");
  EXPECT_EQ(ros2->BuildBaseTopicNameForTest(&dummy), "rt/carla/lidar_top");
  ros2->UnregisterSensor(&dummy);
}

TEST(ros2_topic_name, nonempty_override_is_verbatim) {
  auto ros2 = ROS2::GetInstance();
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "lidar_top", "lidar_top", true,
                       "/sensing/lidar/top/pointcloud_raw_ex");
  EXPECT_EQ(ros2->BuildBaseTopicNameForTest(&dummy),
            "rt/sensing/lidar/top/pointcloud_raw_ex");
  ros2->UnregisterSensor(&dummy);
}

// The override value need not start with '/' — BuildBaseTopicName must still
// prepend exactly one "rt/" (not "rt" + t, which would collide the missing
// slash), matching the leading-slash branch's wire format.
TEST(ros2_topic_name, nonempty_override_without_leading_slash_still_gets_rt_prefix) {
  auto ros2 = ROS2::GetInstance();
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "lidar_top", "lidar_top", true,
                       "sensing/lidar/top/pointcloud_raw_ex");
  EXPECT_EQ(ros2->BuildBaseTopicNameForTest(&dummy),
            "rt/sensing/lidar/top/pointcloud_raw_ex");
  ros2->UnregisterSensor(&dummy);
}

// ComposePointCloudTopic is the DDS-free helper CarlaPointCloudPublisher::Init
// uses to decide whether to append "/<suffix>"; it is pulled out specifically
// so both branches are unit-testable without the Ros2Native/DDS build.
TEST(ros2_topic_name, compose_point_cloud_topic_override_is_verbatim) {
  EXPECT_EQ(
      ComposePointCloudTopic("rt/sensing/lidar/top/pointcloud_raw", true, "point_cloud"),
      "rt/sensing/lidar/top/pointcloud_raw");
}

TEST(ros2_topic_name, compose_point_cloud_topic_default_appends_suffix) {
  EXPECT_EQ(
      ComposePointCloudTopic("rt/carla/lidar_top", false, "point_cloud"),
      "rt/carla/lidar_top/point_cloud");
}

// The following four tests exercise GetOrCreateSensor's Radar and
// RayCastSemanticLidar branches end to end (real publisher construction, real
// DDS Init, same as GetOrCreateTransformPublisherForTest in
// test_ros2_publish_tf.cpp) to pin the fix that threads has_topic_override
// into the whole point-cloud publisher family, not just RayCastLidar: before
// the fix, a radar/semantic-lidar with a non-empty ros_topic_name override
// still got "/point_cloud" appended, because the publisher was always
// constructed with the default has_topic_override=false.
TEST(ros2_topic_name, radar_override_is_verbatim_on_publisher) {
  auto ros2 = ROS2::GetInstance();
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "radar_front", "radar_front", true,
                       "/sensing/radar/front/pointcloud_raw");
  auto publisher = std::dynamic_pointer_cast<CarlaRadarPublisher>(
      ros2->GetOrCreateRadarSensorForTest(1u, &dummy));
  ASSERT_NE(publisher, nullptr);
  EXPECT_TRUE(publisher->HasTopicOverride());
  ros2->UnregisterSensor(&dummy);
}

TEST(ros2_topic_name, radar_default_has_no_override_on_publisher) {
  auto ros2 = ROS2::GetInstance();
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "radar_front", "radar_front", true, "");
  auto publisher = std::dynamic_pointer_cast<CarlaRadarPublisher>(
      ros2->GetOrCreateRadarSensorForTest(1u, &dummy));
  ASSERT_NE(publisher, nullptr);
  EXPECT_FALSE(publisher->HasTopicOverride());
  ros2->UnregisterSensor(&dummy);
}

TEST(ros2_topic_name, semantic_lidar_override_is_verbatim_on_publisher) {
  auto ros2 = ROS2::GetInstance();
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "lidar_top_semantic", "lidar_top_semantic", true,
                       "/sensing/lidar/top/pointcloud_raw_semantic");
  auto publisher = std::dynamic_pointer_cast<CarlaSemanticLidarPublisher>(
      ros2->GetOrCreateSemanticLidarSensorForTest(1u, &dummy));
  ASSERT_NE(publisher, nullptr);
  EXPECT_TRUE(publisher->HasTopicOverride());
  ros2->UnregisterSensor(&dummy);
}

TEST(ros2_topic_name, semantic_lidar_default_has_no_override_on_publisher) {
  auto ros2 = ROS2::GetInstance();
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "lidar_top_semantic", "lidar_top_semantic", true, "");
  auto publisher = std::dynamic_pointer_cast<CarlaSemanticLidarPublisher>(
      ros2->GetOrCreateSemanticLidarSensorForTest(1u, &dummy));
  ASSERT_NE(publisher, nullptr);
  EXPECT_FALSE(publisher->HasTopicOverride());
  ros2->UnregisterSensor(&dummy);
}
#endif  // defined(WITH_ROS2)
