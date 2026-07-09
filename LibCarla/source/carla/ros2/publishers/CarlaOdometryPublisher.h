// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <memory>
#include <string>

#include "carla/ros2/publishers/BasePublisher.h"
#include "carla/ros2/publishers/PublisherImpl.h"

#include "carla/ros2/types/msg/Odometry.h"
#include "carla/ros2/types/msg/Quaternion.h"
#include "carla/ros2/types/msg/Vector3.h"

namespace carla {
namespace ros2 {

  /// Publishes the ground-truth pose and body-frame twist of a registered
  /// vehicle as nav_msgs/Odometry on <base_topic>/odometry, once per frame.
  ///
  /// Callers hand in already ROS-handed message parts (position, orientation
  /// quaternion and twist vectors) so the header does not include
  /// carla::geom types, which pull MsgPack + Boost via carla/MsgPack.h --
  /// neither is on the include path of the carla-ros2-native ExternalProject.
  /// The UE -> ROS conversion lives on the server side (ROS2.cpp) via
  /// UeToRosConversions.h.
  class CarlaOdometryPublisher : public BasePublisher {
    public:
      struct OdometryMsgTraits {
        using msg_type = msg::Odometry;
      };

      CarlaOdometryPublisher(std::string base_topic_name) :
        BasePublisher(base_topic_name + "/odometry"),
        _impl(std::make_shared<PublisherImpl<OdometryMsgTraits>>()) {
          if (!_impl->Init(GetBaseTopicName())) {
            log_warning("CarlaOdometryPublisher: Init failed for topic: ", GetBaseTopicName());
          }
      }

      bool Publish() {
        return _impl->Publish();
      }

      /// @param position vehicle position in ROS axes (meters)
      /// @param orientation vehicle orientation in ROS axes
      /// @param linear_velocity body-frame linear velocity in m/s, ROS axes
      /// @param angular_velocity angular velocity in rad/s, ROS axes
      bool Write(
          int32_t seconds,
          uint32_t nanoseconds,
          std::string frame_id,
          std::string child_frame_id,
          const msg::Vector3 &position,
          const msg::Quaternion &orientation,
          const msg::Vector3 &linear_velocity,
          const msg::Vector3 &angular_velocity);

    private:
      std::shared_ptr<PublisherImpl<OdometryMsgTraits>> _impl;
  };

}  // namespace ros2
}  // namespace carla
