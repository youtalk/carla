// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "CarlaEgoVehicleStatusPublisher.h"

#include <cmath>

namespace carla {
namespace ros2 {

bool CarlaEgoVehicleStatusPublisher::Write(
    int32_t seconds,
    uint32_t nanoseconds,
    std::string frame_id,
    const msg::Quaternion &orientation,
    const msg::Vector3 &linear_velocity,
    float delta_seconds,
    const msg::CarlaEgoVehicleControl &control) {
  auto message = _impl->GetMessage();

  message->header.stamp.sec = seconds;
  message->header.stamp.nanosec = nanoseconds;
  message->header.frame_id = frame_id;

  // Speed is the magnitude of the velocity; the y-axis sign flip applied on
  // the server side does not change it.
  message->velocity = static_cast<float>(std::sqrt(
      linear_velocity.x * linear_velocity.x +
      linear_velocity.y * linear_velocity.y +
      linear_velocity.z * linear_velocity.z));

  // Acceleration from the per-publisher velocity history; the angular part
  // stays zero, mirroring the ros-bridge CarlaEgoVehicleStatus semantics.
  message->acceleration.linear = ComputeAcceleration(
      linear_velocity, _previous_velocity, _has_previous_velocity, delta_seconds);
  _previous_velocity = linear_velocity;
  _has_previous_velocity = true;

  message->orientation = orientation;
  message->control = control;

  return true;
}

}  // namespace ros2
}  // namespace carla
