// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/ros2/publishers/CarlaPointCloudPublisher.h"

#include "carla/Logging.h"
#include "carla/ros2/publishers/PointCloudTopic.h"
#include "carla/ros2/publishers/PublisherImpl.h"
#include "carla/ros2/types/msg/PointCloud2.h"
#include "carla/ros2/types/msg/PointField.h"

#include <string>
#include <utility>
#include <vector>

namespace carla {
namespace ros2 {

struct CarlaPointCloudMsgTraits {
  using msg_type = msg::PointCloud2;
};

namespace {

// Translates the FastDDS-free PointFieldDataType enum (defined in
// PointCloudFieldsLayout.h) to the carla::ros2::msg::PointField::* numeric
// constants the message struct expects on the wire.
std::uint8_t ToPointFieldDatatype(PointFieldDataType datatype) noexcept {
  switch (datatype) {
    case PointFieldDataType::Int8:    return msg::PointField::INT8;
    case PointFieldDataType::UInt8:   return msg::PointField::UINT8;
    case PointFieldDataType::Int16:   return msg::PointField::INT16;
    case PointFieldDataType::UInt16:  return msg::PointField::UINT16;
    case PointFieldDataType::Int32:   return msg::PointField::INT32;
    case PointFieldDataType::UInt32:  return msg::PointField::UINT32;
    case PointFieldDataType::Float32: return msg::PointField::FLOAT32;
    case PointFieldDataType::Float64: return msg::PointField::FLOAT64;
  }
  return msg::PointField::FLOAT32;
}

std::vector<msg::PointField> BuildPointFields(
    const PointFieldDescriptor *descriptors, std::size_t count) {
  std::vector<msg::PointField> fields;
  fields.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto &descriptor = descriptors[i];
    msg::PointField field{};
    field.name = std::string{descriptor.name};
    field.offset = descriptor.offset;
    field.datatype = ToPointFieldDatatype(descriptor.datatype);
    field.count = descriptor.count;
    fields.push_back(std::move(field));
  }
  return fields;
}

}  // namespace

CarlaPointCloudPublisher::CarlaPointCloudPublisher(
    std::string base_topic_name, std::string frame_id, bool has_topic_override,
    PublisherQos qos)
  : BasePublisher(std::move(base_topic_name), std::move(frame_id)),
    _impl(std::make_shared<PublisherImpl<CarlaPointCloudMsgTraits>>()) {
  // qos defaults to SensorData() (best-effort): point clouds are large and
  // per-tick, so a slow subscriber must never block the publishing thread by
  // default. CarlaLidarPublisher forwards the per-sensor
  // ros2_qos_reliability/durability/history_depth blueprint attributes here
  // (see ActorDispatcher::RegisterActor), letting Autoware-facing lidars
  // match the AWSIM/tier4 BEST_EFFORT/VOLATILE/depth-5 profile exactly. A
  // verbatim ros_topic_name override is published as-is; otherwise the
  // default composition gets the "/point_cloud" suffix (see
  // ComposePointCloudTopic, unit-tested directly in test_ros2_topic_name.cpp).
  const std::string topic =
      ComposePointCloudTopic(GetBaseTopicName(), has_topic_override, "point_cloud");
  if (!_impl->Init(topic, qos)) {
    log_error("CarlaPointCloudPublisher: failed to initialise writer for", topic);
  }
}

CarlaPointCloudPublisher::~CarlaPointCloudPublisher() = default;

bool CarlaPointCloudPublisher::Publish() {
  return _impl->Publish();
}

bool CarlaPointCloudPublisher::WritePointCloud(
    std::int32_t seconds,
    std::uint32_t nanoseconds,
    std::uint32_t height,
    std::uint32_t width,
    const std::uint8_t *data) {
  return WritePointCloud(
      seconds, nanoseconds, height, width,
      ComputePointCloud(height, width, data));
}

bool CarlaPointCloudPublisher::WritePointCloud(
    std::int32_t seconds,
    std::uint32_t nanoseconds,
    std::uint32_t height,
    std::uint32_t width,
    std::vector<std::uint8_t> data) {
  auto *message = _impl->GetMessage();
  message->header.stamp.sec = seconds;
  message->header.stamp.nanosec = nanoseconds;
  message->header.frame_id = GetFrameId();

  const std::size_t point_size = GetPointSize();

  message->width = width;
  message->height = height;
  message->is_bigendian = false;
  message->fields = BuildPointFields(GetFieldDescriptors(), GetFieldDescriptorCount());
  message->point_step = static_cast<std::uint32_t>(point_size);
  message->row_step = static_cast<std::uint32_t>(width * point_size);
  // is_dense=false matches the upstream convention: lidar / radar scans contain
  // invalid points (no return / max-range hits) and subscribers must not
  // assume tightly packed valid data.
  message->is_dense = false;
  message->data = std::move(data);

  return true;
}

}  // namespace ros2
}  // namespace carla
