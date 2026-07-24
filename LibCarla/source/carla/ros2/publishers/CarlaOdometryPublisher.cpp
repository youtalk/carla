// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB).
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "CarlaOdometryPublisher.h"

#include "carla/Logging.h"
#include "carla/ros2/publishers/PublisherImpl.h"
#include "carla/ros2/types/msg/Odometry.h"

namespace carla {
namespace ros2 {

struct OdometryMsgTraits {
  using msg_type = msg::Odometry;
};

CarlaOdometryPublisher::CarlaOdometryPublisher(std::string base_topic_name)
  : BasePublisher(base_topic_name + "/odometry"),
    _impl(std::make_shared<PublisherImpl<OdometryMsgTraits>>()) {
  if (!_impl->Init(GetBaseTopicName())) {
    log_error("CarlaOdometryPublisher: Init failed for topic: ", GetBaseTopicName());
  }
}

CarlaOdometryPublisher::~CarlaOdometryPublisher() = default;

bool CarlaOdometryPublisher::Publish() {
  return _impl->Publish();
}

bool CarlaOdometryPublisher::Write(
    int32_t seconds,
    uint32_t nanoseconds,
    std::string frame_id,
    std::string child_frame_id,
    const msg::Vector3 &position,
    const msg::Quaternion &orientation,
    const msg::Vector3 &linear_velocity,
    const msg::Vector3 &angular_velocity) {
  auto message = _impl->GetMessage();

  message->header.stamp.sec = seconds;
  message->header.stamp.nanosec = nanoseconds;
  message->header.frame_id = frame_id;
  message->child_frame_id = child_frame_id;

  message->pose.pose.position.x = position.x;
  message->pose.pose.position.y = position.y;
  message->pose.pose.position.z = position.z;
  message->pose.pose.orientation = orientation;

  // Twist linear velocity is expressed in the child (vehicle) frame per the
  // nav_msgs/Odometry convention.
  message->twist.twist.linear = linear_velocity;
  message->twist.twist.angular = angular_velocity;

  return true;
}

}  // namespace ros2
}  // namespace carla
