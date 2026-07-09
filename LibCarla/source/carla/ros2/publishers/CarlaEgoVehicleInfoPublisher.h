// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <memory>
#include <string>

#include "carla/ros2/publishers/BasePublisher.h"
#include "carla/ros2/publishers/PublisherImpl.h"

#include "carla/ros2/types/msg/CarlaEgoVehicleInfo.h"

namespace carla {
namespace ros2 {

  /// Publishes the static description of a registered vehicle as a latched
  /// carla_msgs/CarlaEgoVehicleInfo on <base_topic>/vehicle_info, once at
  /// registration. The transient_local durability lets late-joining
  /// subscribers receive it without CARLA re-publishing.
  ///
  /// The caller hands in the already ROS-handed message so the header does not
  /// include carla::geom / carla::rpc types, which pull MsgPack + Boost via
  /// carla/MsgPack.h -- neither is on the include path of the
  /// carla-ros2-native ExternalProject. The UE -> ROS conversion lives on the
  /// server side (ROS2.cpp) via UeToRosConversions.h.
  class CarlaEgoVehicleInfoPublisher : public BasePublisher {
    public:
      struct InfoMsgTraits {
        using msg_type = msg::CarlaEgoVehicleInfo;
      };

      CarlaEgoVehicleInfoPublisher(std::string base_topic_name) :
        BasePublisher(base_topic_name + "/vehicle_info"),
        _impl(std::make_shared<PublisherImpl<InfoMsgTraits>>()) {
          PublisherQos qos;
          qos.durability = DurabilityKind::TransientLocal;
          if (!_impl->Init(GetBaseTopicName(), qos)) {
            log_warning("CarlaEgoVehicleInfoPublisher: Init failed for topic: ", GetBaseTopicName());
          }
      }

      bool Publish() {
        return _impl->Publish();
      }

      /// @param info static vehicle description in ROS coordinates and units
      bool Write(const msg::CarlaEgoVehicleInfo &info);

    private:
      std::shared_ptr<PublisherImpl<InfoMsgTraits>> _impl;
  };

}  // namespace ros2
}  // namespace carla
