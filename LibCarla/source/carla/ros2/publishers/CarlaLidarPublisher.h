// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include "carla/ros2/publishers/CarlaPointCloudPublisher.h"

namespace carla {
namespace ros2 {

class CarlaLidarPublisher : public CarlaPointCloudPublisher {
public:
  // extended selects the opt-in 10-float PointXYZIRCAEDT layout (blueprint
  // attribute ros2_extended_lidar, threaded via ActorRegistration): when true
  // every layout accessor and ComputePointCloud below switches to the 32-byte
  // LidarPointEx wire form. Defaults to false so an unadorned lidar is
  // byte-identical to the pre-Task-9 16-byte XYZI publisher. ProcessDataFromLidar
  // must feed WritePointCloud the matching stride (LidarPointEx[] vs the flat
  // 4-float _points buffer); IsExtended() lets it pick the right one.
  CarlaLidarPublisher(
      std::string base_topic_name, std::string frame_id,
      bool has_topic_override = false,
      PublisherQos qos = PublisherQos::SensorData(),
      bool extended = false)
    : CarlaPointCloudPublisher(
          std::move(base_topic_name), std::move(frame_id), has_topic_override,
          std::move(qos)),
      _extended(extended) {}

  [[nodiscard]] bool IsExtended() const { return _extended; }

private:
  [[nodiscard]] std::size_t GetPointSize() const override;
  [[nodiscard]] const PointFieldDescriptor *GetFieldDescriptors() const override;
  [[nodiscard]] std::size_t GetFieldDescriptorCount() const override;
  [[nodiscard]] std::vector<std::uint8_t> ComputePointCloud(
      std::uint32_t height, std::uint32_t width, const std::uint8_t *data) const override;

  bool _extended = false;
};

}  // namespace ros2
}  // namespace carla
