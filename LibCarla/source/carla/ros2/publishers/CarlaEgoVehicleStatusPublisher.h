// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <memory>
#include <string>

#include "carla/ros2/publishers/BasePublisher.h"

#include "carla/ros2/types/msg/CarlaEgoVehicleControl.h"
#include "carla/ros2/types/msg/Quaternion.h"
#include "carla/ros2/types/msg/Vector3.h"

namespace carla {
namespace ros2 {

  // Forward declarations keep the FastDDS-heavy PublisherImpl<> definition out
  // of the main carla-server compile unit. The full template definition + the
  // StatusMsgTraits instantiation live in CarlaEgoVehicleStatusPublisher.cpp,
  // which is built by the carla-ros2-native ExternalProject (where the
  // middleware macros and the vendor headers are on the include path). Defining
  // the constructor in the header instead lets the macro-less carla-server
  // instantiation of PublisherImpl<>::Init win at link time, where the
  // middleware factory yields nullptr and the DDS writer is never created. Same
  // pattern as the neighboring ported publishers (CarlaClockPublisher.{h,cpp}).
  template <typename Traits> class PublisherImpl;
  struct StatusMsgTraits;

  /// Publishes the current speed, acceleration, orientation and applied
  /// control of a registered vehicle as carla_msgs/CarlaEgoVehicleStatus on
  /// <base_topic>/vehicle_status, once per frame. Acceleration is derived
  /// from the velocity of the previous frame, so the first frame after
  /// registration reports zero acceleration.
  ///
  /// Callers hand in already ROS-handed message parts so the header does not
  /// include carla::geom / carla::rpc types, which pull MsgPack + Boost via
  /// carla/MsgPack.h -- neither is on the include path of the
  /// carla-ros2-native ExternalProject. The UE -> ROS conversion lives on the
  /// server side (ROS2.cpp) via UeToRosConversions.h.
  class CarlaEgoVehicleStatusPublisher : public BasePublisher {
    public:
      CarlaEgoVehicleStatusPublisher(std::string base_topic_name);
      ~CarlaEgoVehicleStatusPublisher() override;

      CarlaEgoVehicleStatusPublisher(const CarlaEgoVehicleStatusPublisher &) = delete;
      CarlaEgoVehicleStatusPublisher &operator=(const CarlaEgoVehicleStatusPublisher &) = delete;
      CarlaEgoVehicleStatusPublisher(CarlaEgoVehicleStatusPublisher &&) noexcept = default;
      CarlaEgoVehicleStatusPublisher &operator=(CarlaEgoVehicleStatusPublisher &&) noexcept = default;

      bool Publish() override;

      /// @param orientation vehicle orientation in ROS axes
      /// @param linear_velocity world-frame velocity in m/s, ROS axes
      /// @param delta_seconds simulation time elapsed since the previous frame
      /// @param control last control applied to the vehicle, echoed back
      bool Write(
          int32_t seconds,
          uint32_t nanoseconds,
          std::string frame_id,
          const msg::Quaternion &orientation,
          const msg::Vector3 &linear_velocity,
          float delta_seconds,
          const msg::CarlaEgoVehicleControl &control);

      /// Returns (velocity - previous_velocity) / delta_seconds, or a zero
      /// vector when there is no previous sample or the delta is not positive.
      /// Templated on the vector type so this header stays free of
      /// carla::geom (which pulls MsgPack + Boost); the server side
      /// instantiates it with carla::geom::Vector3D and the publisher with
      /// carla::ros2::msg::Vector3.
      template <typename VectorT>
      static VectorT ComputeAcceleration(
          const VectorT &velocity,
          const VectorT &previous_velocity,
          bool has_previous_velocity,
          float delta_seconds) {
        if (!has_previous_velocity || delta_seconds <= 0.0f) {
          return VectorT();
        }
        VectorT acceleration;
        acceleration.x = (velocity.x - previous_velocity.x) / delta_seconds;
        acceleration.y = (velocity.y - previous_velocity.y) / delta_seconds;
        acceleration.z = (velocity.z - previous_velocity.z) / delta_seconds;
        return acceleration;
      }

    private:
      std::shared_ptr<PublisherImpl<StatusMsgTraits>> _impl;
      msg::Vector3 _previous_velocity;
      bool _has_previous_velocity{false};
  };

}  // namespace ros2
}  // namespace carla
