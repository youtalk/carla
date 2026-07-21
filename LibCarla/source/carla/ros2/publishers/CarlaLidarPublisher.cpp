// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/ros2/publishers/CarlaLidarPublisher.h"

#include "carla/ros2/publishers/ExtendedLidarPoint.h"

#include <cstring>

namespace carla {
namespace ros2 {

namespace {

// Wire-compatible POD mirror of sensor::data::LidarDetection: {x, y, z,
// intensity} as four contiguous float32s. The real struct lives behind
// carla/sensor/data/LidarData.h, which transitively includes
// carla/MsgPack.h + carla/Buffer.h and therefore pulls Boost — not
// available on the carla-ros2-native ExternalProject include path. The
// POD shape and stride (16 B per point) are pinned by kLidarFields in
// PointCloudFieldsLayout.h and by test_point_cloud_fields.cpp.
struct LidarPoint {
  float x;
  float y;
  float z;
  float intensity;
};

static_assert(
    sizeof(LidarPoint) == 16u,
    "LidarPoint must be tightly packed (4 float32s) for the wire layout");

}  // namespace

std::size_t CarlaLidarPublisher::GetPointSize() const {
  return _extended ? sizeof(LidarPointEx) : sizeof(LidarPoint);
}

const PointFieldDescriptor *CarlaLidarPublisher::GetFieldDescriptors() const {
  return _extended ? kLidarFieldsExtended.data() : kLidarFields.data();
}

std::size_t CarlaLidarPublisher::GetFieldDescriptorCount() const {
  return _extended ? kLidarFieldsExtended.size() : kLidarFields.size();
}

std::vector<std::uint8_t> CarlaLidarPublisher::ComputePointCloud(
    std::uint32_t height, std::uint32_t width, const std::uint8_t *data) const {
  const std::size_t total_points =
      static_cast<std::size_t>(height) * static_cast<std::size_t>(width);

  if (_extended) {
    // Extended path: the input is a contiguous LidarPointEx[] assembled by
    // ProcessDataFromLidar (unflipped, CARLA/UE frame). Copy it, then map each
    // point into the ROS right-handed frame — the same Y negation the legacy
    // path applies below, but here azimuth's sign flips with y too (see
    // ApplyRosFrameFlip). x/z/intensity/return_type/channel/elevation/distance/
    // time_stamp pass through unchanged.
    const std::size_t total_bytes = total_points * sizeof(LidarPointEx);
    std::vector<std::uint8_t> bytes(total_bytes);
    std::memcpy(bytes.data(), data, total_bytes);
    auto *points = reinterpret_cast<LidarPointEx *>(bytes.data());
    for (std::size_t i = 0; i < total_points; ++i) {
      ApplyRosFrameFlip(points[i]);
    }
    return bytes;
  }

  const std::size_t total_bytes = total_points * sizeof(LidarPoint);

  std::vector<std::uint8_t> bytes(total_bytes);
  std::memcpy(bytes.data(), data, total_bytes);

  // Mirror the Y axis to land in the ROS right-handed frame. The buffer is
  // a contiguous array of LidarPoint PODs by contract; aliasing into the
  // copy is safe.
  auto *points = reinterpret_cast<LidarPoint *>(bytes.data());
  for (std::size_t i = 0; i < total_points; ++i) {
    points[i].y *= -1.0f;
  }
  return bytes;
}

}  // namespace ros2
}  // namespace carla
