// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include "carla/ros2/publishers/CarlaPointCloudPublisher.h"

namespace carla {
namespace ros2 {

class CarlaSemanticLidarPublisher : public CarlaPointCloudPublisher {
public:
  // has_topic_override forwards verbatim to CarlaPointCloudPublisher: see its
  // ctor comment for the "/point_cloud" suffix-suppression rationale, mirrored
  // here from CarlaLidarPublisher for the whole point-cloud publisher family.
  CarlaSemanticLidarPublisher(
      std::string base_topic_name, std::string frame_id,
      bool has_topic_override = false)
    : CarlaPointCloudPublisher(
          std::move(base_topic_name), std::move(frame_id), has_topic_override) {}

private:
  [[nodiscard]] std::size_t GetPointSize() const override;
  [[nodiscard]] const PointFieldDescriptor *GetFieldDescriptors() const override;
  [[nodiscard]] std::size_t GetFieldDescriptorCount() const override;
  [[nodiscard]] std::vector<std::uint8_t> ComputePointCloud(
      std::uint32_t height, std::uint32_t width, const std::uint8_t *data) const override;
};

}  // namespace ros2
}  // namespace carla
