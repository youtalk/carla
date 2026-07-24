// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <memory>
#include <string>

#include "carla/ros2/publishers/BasePublisher.h"

#include "carla/ros2/types/msg/CarlaEgoVehicleInfo.h"

namespace carla {
namespace ros2 {

  // Forward declarations keep the FastDDS-heavy PublisherImpl<> definition out
  // of the main carla-server compile unit. The full template definition + the
  // InfoMsgTraits instantiation live in CarlaEgoVehicleInfoPublisher.cpp, which
  // is built by the carla-ros2-native ExternalProject (where the middleware
  // macros and the vendor headers are on the include path). Defining the
  // constructor in the header instead lets the macro-less carla-server
  // instantiation of PublisherImpl<>::Init win at link time, where the
  // middleware factory yields nullptr and the DDS writer is never created. Same
  // pattern as the neighboring ported publishers (CarlaClockPublisher.{h,cpp}).
  template <typename Traits> class PublisherImpl;
  struct InfoMsgTraits;

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
      CarlaEgoVehicleInfoPublisher(std::string base_topic_name);
      ~CarlaEgoVehicleInfoPublisher() override;

      CarlaEgoVehicleInfoPublisher(const CarlaEgoVehicleInfoPublisher &) = delete;
      CarlaEgoVehicleInfoPublisher &operator=(const CarlaEgoVehicleInfoPublisher &) = delete;
      CarlaEgoVehicleInfoPublisher(CarlaEgoVehicleInfoPublisher &&) noexcept = default;
      CarlaEgoVehicleInfoPublisher &operator=(CarlaEgoVehicleInfoPublisher &&) noexcept = default;

      bool Publish() override;

      /// @param info static vehicle description in ROS coordinates and units
      bool Write(const msg::CarlaEgoVehicleInfo &info);

    private:
      std::shared_ptr<PublisherImpl<InfoMsgTraits>> _impl;
  };

}  // namespace ros2
}  // namespace carla
