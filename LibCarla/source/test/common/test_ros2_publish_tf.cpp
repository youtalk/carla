// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include <gtest/gtest.h>

// See test_ros2_topic_name.cpp for why this whole TU is gated: carla/ros2/*.cpp
// (ROS2.cpp, the real target here) is only compiled into carla-server (see
// LibCarla/CMakeLists.txt's ENABLE_ROS2 glob), which is what
// libcarla_test_server links. test/common/*.cpp is also compiled into
// libcarla_test_client (linked against carla-client), which never builds
// ROS2.cpp, so calling ROS2:: actor-registration APIs there is an undefined
// reference at link time. WITH_ROS2 is carla-server's own PUBLIC compile
// definition (propagates to libcarla_test_server, not libcarla_test_client),
// so guarding on it keeps this test file a no-op TU on the client side
// instead of a link failure.
#if defined(WITH_ROS2)
#include "carla/ros2/ROS2.h"

using carla::ros2::ROS2;

TEST(ros2_publish_tf, default_is_true) {
  EXPECT_TRUE(ROS2::GetInstance()->GetPublishTF());
}

// Positive-path pin: without this, a GetOrCreateTransformPublisher that
// always returned nullptr (e.g. an inverted flag check, or an early-return
// that fires unconditionally) would still pass the rest of this suite, since
// every other test here only asserts the nullptr/suppressed side.
TEST(ros2_publish_tf, enabled_registers_transform_publisher) {
  auto ros2 = ROS2::GetInstance();
  ros2->SetPublishTF(true);   // don't depend on another test's cleanup
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "lidar", "lidar", /*publish_tf=*/true, "");
  EXPECT_NE(ros2->GetOrCreateTransformPublisherForTest(&dummy), nullptr);
  ros2->UnregisterSensor(&dummy);
}

TEST(ros2_publish_tf, set_false_suppresses_all_tf) {
  auto ros2 = ROS2::GetInstance();
  ros2->SetPublishTF(false);
  EXPECT_FALSE(ros2->GetPublishTF());
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "lidar", "lidar", /*publish_tf=*/true, "");
  EXPECT_EQ(ros2->GetOrCreateTransformPublisherForTest(&dummy), nullptr);
  ros2->UnregisterSensor(&dummy);
  ros2->SetPublishTF(true);   // restore for other tests
}

// Cache-hit-then-suppress pin: matches the live-verified behavior where an
// already-lazily-created transform publisher goes silent the moment global
// suppression is set, without needing
// re-registration. This only passes because ROS2::GetOrCreateTransformPublisher
// checks _publish_tf_global *before* consulting the _transforms cache; a
// refactor that moved the flag check below the cache lookup would keep
// returning the cached (non-null) publisher here and fail this test, even
// though set_false_suppresses_all_tf above would still pass (that test never
// primes the cache before suppressing).
TEST(ros2_publish_tf, suppressing_after_cache_hit_still_returns_nullptr) {
  auto ros2 = ROS2::GetInstance();
  ros2->SetPublishTF(true);
  int dummy = 0;
  ros2->RegisterSensor(&dummy, "lidar", "lidar", /*publish_tf=*/true, "");
  ASSERT_NE(ros2->GetOrCreateTransformPublisherForTest(&dummy), nullptr);  // creates + caches
  ros2->SetPublishTF(false);
  EXPECT_EQ(ros2->GetOrCreateTransformPublisherForTest(&dummy), nullptr);  // same actor, cached entry, now suppressed
  ros2->UnregisterSensor(&dummy);
  ros2->SetPublishTF(true);   // restore for other tests
}
#endif  // defined(WITH_ROS2)
