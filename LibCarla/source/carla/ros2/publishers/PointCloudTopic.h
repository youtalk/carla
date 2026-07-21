// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <string>

namespace carla {
namespace ros2 {

// Pure, DDS-free topic-composition helper for the PointCloud2 publisher
// family (lidar, semantic-lidar, radar, DVS point-cloud). Kept in its own
// header — like PointCloudFieldsLayout.h — so it can be unit-tested directly
// from carla-server-linked test binaries without pulling in PublisherImpl<>/
// DDS types, which live only in the Ros2Native build (see the two-build-split
// note in ROS2.cpp). A verbatim ros_topic_name override (has_topic_override)
// publishes on base_topic exactly as configured; otherwise the default
// composition appends "/<suffix>" (e.g. "point_cloud").
inline std::string ComposePointCloudTopic(
    const std::string &base_topic, bool has_topic_override, const char *suffix) {
  return has_topic_override ? base_topic : base_topic + "/" + suffix;
}

}  // namespace ros2
}  // namespace carla
