// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <memory>
#include <string>

#include "carla/ros2/publishers/BasePublisher.h"

#include "carla/ros2/types/msg/Quaternion.h"
#include "carla/ros2/types/msg/Vector3.h"

namespace carla {
namespace ros2 {

  // Forward declarations keep the FastDDS-heavy PublisherImpl<> definition out
  // of the main carla-server compile unit. The full template definition + the
  // OdometryMsgTraits instantiation live in CarlaOdometryPublisher.cpp, which is
  // built by the carla-ros2-native ExternalProject (where the middleware macros
  // and the vendor headers are on the include path). Defining the constructor
  // in the header instead lets the macro-less carla-server instantiation of
  // PublisherImpl<>::Init win at link time, where the middleware factory yields
  // nullptr and the DDS writer is never created. Same pattern as the
  // neighboring ported publishers (CarlaClockPublisher.{h,cpp}).
  template <typename Traits> class PublisherImpl;
  struct OdometryMsgTraits;

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
      CarlaOdometryPublisher(std::string base_topic_name);
      ~CarlaOdometryPublisher() override;

      CarlaOdometryPublisher(const CarlaOdometryPublisher &) = delete;
      CarlaOdometryPublisher &operator=(const CarlaOdometryPublisher &) = delete;
      CarlaOdometryPublisher(CarlaOdometryPublisher &&) noexcept = default;
      CarlaOdometryPublisher &operator=(CarlaOdometryPublisher &&) noexcept = default;

      bool Publish() override;

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
