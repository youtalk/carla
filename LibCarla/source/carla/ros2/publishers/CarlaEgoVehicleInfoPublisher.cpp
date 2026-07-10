// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "CarlaEgoVehicleInfoPublisher.h"

#include "carla/Logging.h"
#include "carla/ros2/middleware/PublisherQos.h"
#include "carla/ros2/publishers/PublisherImpl.h"
#include "carla/ros2/types/msg/CarlaEgoVehicleInfo.h"

namespace carla {
namespace ros2 {

struct InfoMsgTraits {
  using msg_type = msg::CarlaEgoVehicleInfo;
};

CarlaEgoVehicleInfoPublisher::CarlaEgoVehicleInfoPublisher(std::string base_topic_name)
  : BasePublisher(base_topic_name + "/vehicle_info"),
    _impl(std::make_shared<PublisherImpl<InfoMsgTraits>>()) {
  // Latched topic: transient_local durability so a late-joining subscriber
  // still receives the static vehicle description without CARLA re-publishing.
  PublisherQos qos;
  qos.durability = DurabilityKind::TransientLocal;
  if (!_impl->Init(GetBaseTopicName(), qos)) {
    log_error("CarlaEgoVehicleInfoPublisher: Init failed for topic: ", GetBaseTopicName());
  }
}

CarlaEgoVehicleInfoPublisher::~CarlaEgoVehicleInfoPublisher() = default;

bool CarlaEgoVehicleInfoPublisher::Publish() {
  return _impl->Publish();
}

bool CarlaEgoVehicleInfoPublisher::Write(const msg::CarlaEgoVehicleInfo &info) {
  auto message = _impl->GetMessage();
  *message = info;
  return true;
}

}  // namespace ros2
}  // namespace carla
