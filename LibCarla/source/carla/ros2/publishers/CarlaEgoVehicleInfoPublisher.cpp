// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "CarlaEgoVehicleInfoPublisher.h"

namespace carla {
namespace ros2 {

bool CarlaEgoVehicleInfoPublisher::Write(const msg::CarlaEgoVehicleInfo &info) {
  auto message = _impl->GetMessage();
  *message = info;
  return true;
}

}  // namespace ros2
}  // namespace carla
