// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include "carla/ros2/publishers/BasePublisher.h"

#include <memory>
#include <string>

namespace carla {
namespace ros2 {

  // Forward declarations keep the FastDDS-heavy PublisherImpl<> definition out
  // of the main carla-server compile unit. The full template definition + the
  // MapMsgTraits instantiation live in CarlaMapPublisher.cpp, which is built by
  // the carla-ros2-native ExternalProject (where the middleware macros and the
  // vendor headers are on the include path). Same pattern as the neighboring
  // ported publishers (CarlaClockPublisher.{h,cpp}).
  template <typename Traits> class PublisherImpl;
  struct CarlaMapMsgTraits;

  /// Publishes the OpenDRIVE description of the current map as a latched
  /// std_msgs/String on rt/carla/map. The transient_local durability lets
  /// late-joining subscribers receive the map without CARLA re-publishing it.
  ///
  /// std_msgs/String carries no Header, and that is deliberate: it keeps the
  /// topic wire-compatible with carla-ros-bridge's /carla/map, which existing
  /// tooling already consumes. The message therefore has no stamp or episode
  /// id of its own; its identity contract is positional instead. The writer
  /// cache holds exactly one sample (keep-last depth 1) and the map is
  /// re-published on every episode start / map load, overwriting that sample,
  /// so the latched payload a late joiner receives always describes the map
  /// of the current episode. Consumers that need to detect a map change
  /// should watch for a new sample arriving on this topic (the OpenDRIVE
  /// header's own revision/name attributes identify the map) rather than
  /// expect a ROS stamp.
  class CarlaMapPublisher : public BasePublisher {
    public:
      CarlaMapPublisher();
      ~CarlaMapPublisher() override;

      CarlaMapPublisher(const CarlaMapPublisher &) = delete;
      CarlaMapPublisher &operator=(const CarlaMapPublisher &) = delete;
      CarlaMapPublisher(CarlaMapPublisher &&) noexcept = default;
      CarlaMapPublisher &operator=(CarlaMapPublisher &&) noexcept = default;

      bool Publish() override;
      bool Write(const std::string& open_drive);

    private:
      std::shared_ptr<PublisherImpl<CarlaMapMsgTraits>> _impl;
  };

}  // namespace ros2
}  // namespace carla
