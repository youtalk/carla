// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/Logging.h"
#include "carla/ros2/ROS2.h"
#include "carla/geom/GeoLocation.h"
#include "carla/geom/Vector3D.h"
#include "carla/rpc/VehicleControl.h"
#include "carla/rpc/VehiclePhysicsControl.h"
#include "carla/sensor/data/DVSEvent.h"
#include "carla/sensor/data/LidarData.h"
#include "carla/sensor/data/SemanticLidarData.h"
#include "carla/sensor/data/RadarData.h"
#include "carla/sensor/data/Image.h"
#include "carla/sensor/s11n/ImageSerializer.h"
#include "carla/sensor/s11n/SensorHeaderSerializer.h"

#include "carla/ros2/middleware/ActiveMiddleware.h"

#include "publishers/BasePublisher.h"
#include "publishers/CarlaCameraPublisher.h"
#include "publishers/CarlaClockPublisher.h"
#include "publishers/CarlaRGBCameraPublisher.h"
#include "publishers/CarlaDepthCameraPublisher.h"
#include "publishers/CarlaEgoVehicleInfoPublisher.h"
#include "publishers/CarlaEgoVehicleStatusPublisher.h"
#include "publishers/CarlaMapPublisher.h"
#include "publishers/CarlaNormalsCameraPublisher.h"
#include "publishers/CarlaOdometryPublisher.h"
#include "publishers/CarlaOpticalFlowCameraPublisher.h"
#include "publishers/CarlaSSCameraPublisher.h"
#include "publishers/CarlaISCameraPublisher.h"
#include "publishers/CarlaDVSCameraPublisher.h"
#include "publishers/CarlaLidarPublisher.h"
#include "publishers/CarlaSemanticLidarPublisher.h"
#include "publishers/CarlaRadarPublisher.h"
#include "publishers/CarlaIMUPublisher.h"
#include "publishers/CarlaGNSSPublisher.h"
#include "publishers/CarlaTransformPublisher.h"
#include "publishers/UeToRosConversions.h"
#include "publishers/CarlaCollisionPublisher.h"
#include "publishers/BasicPublisher.h"

#include "subscribers/AckermannControlSubscriber.h"
#include "subscribers/BaseSubscriber.h"
#include "subscribers/CarlaEgoVehicleControlSubscriber.h"
#include "subscribers/CarlaSubscriber.h"
#if defined(WITH_ROS2_DEMO)
  #include "subscribers/BasicSubscriber.h"
#endif

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace carla {
namespace ros2 {

// static fields
std::shared_ptr<ROS2> ROS2::_instance;

// list of sensors (should be equal to the list of SensorsRegistry)
enum ESensors {
  CollisionSensor,
  DepthCamera,
  NormalsCamera,
  DVSCamera,
  GnssSensor,
  InertialMeasurementUnit,
  LaneInvasionSensor,
  ObstacleDetectionSensor,
  OpticalFlowCamera,
  Radar,
  RayCastSemanticLidar,
  RayCastLidar,
  RssSensor,
  SceneCaptureCamera,
  SemanticSegmentationCamera,
  InstanceSegmentationCamera,
  WorldObserver,
  // Keep these in lock-step with the SensorRegistry tuple order: the fisheye /
  // wide-angle-lens cameras (ported in #9741) were added to SensorRegistry but
  // were missing here, which shifted every following value out of sync with the
  // registry index.
  SceneCaptureCamera_WideAngleLens,
  DepthCamera_WideAngleLens,
  InstanceSegmentationCamera_WideAngleLens,
  SemanticSegmentationCamera_WideAngleLens,
  CameraGBufferUint8,
  CameraGBufferFloat,
  HSSLidar
};

bool ROS2::Enable(bool enable, Middleware middleware, int domain_id) {
  // Select the ROS 2 middleware before any publisher or subscriber is created.
  // SetActiveMiddleware is the DDS-free bridge that resolves availability inside
  // the shared library (the CARLA_ROS2_MIDDLEWARE_* macros are not visible here
  // in carla-server) and keeps vendor headers out of carla-server.
  if (enable && !SetActiveMiddleware(middleware)) {
    log_error("ROS2: requested middleware '", MiddlewareToString(middleware),
              "' is not available. Compiled in: ", GetAvailableMiddleware(), ".");
    _enabled = false;
    return false;
  }
  if (enable) {
    // Configure the domain id before any transport context is created (the
    // shared participants/sessions are created lazily on the first publisher,
    // i.e. the clock publisher below). SetActiveDomainId is the DDS-free bridge
    // so the value lands in the shared library's MiddlewareConfig, which the
    // middlewares read.
    const ResolvedDomainId resolved = SetActiveDomainId(domain_id);
    const char* domain_source =
        (resolved.source == DomainIdSource::CommandLine)  ? "--ros-domain-id"
        : (resolved.source == DomainIdSource::Environment) ? "ROS_DOMAIN_ID"
                                                           : "default";
    log_info("ROS2: using middleware '", MiddlewareToString(middleware),
        "', domain id: ", resolved.id, " (", domain_source, ")");
  }
  _enabled = enable;
  log_info("ROS2 enabled: ", _enabled);
  _clock_publisher = std::make_shared<CarlaClockPublisher>();
#if defined(WITH_ROS2_DEMO)
  _basic_publisher = std::make_shared<BasicPublisher>();
  _basic_publisher->Init();
#endif
  return true;
}

void ROS2::SetFrame(uint64_t frame) {
  _frame = frame;
  for (auto &element : _subscribers) {
    void *actor = element.first;
    auto &subscriber = element.second;
    auto callback_it = _actor_callbacks.find(actor);
    if (callback_it != _actor_callbacks.end()) {
      subscriber->ProcessMessages(callback_it->second);
    }
  }
#if defined(WITH_ROS2_DEMO)
  if (_basic_subscriber) {
    void *actor = _basic_subscriber->GetActor();
    if (!_basic_subscriber->IsAlive()) {
      RemoveBasicSubscriberCallback(actor);
    }
    if (actor && _basic_subscriber->HasNewMessage()) {
      auto it = _actor_message_callbacks.find(actor);
      if (it != _actor_message_callbacks.end()) {
        MessageControl control;
        control.message = _basic_subscriber->GetMessage();
        it->second(actor, control);
      }
    }
  }
#endif
}

void ROS2::SetTimestamp(double timestamp) {
  double integral;
  const double fractional = std::modf(timestamp, &integral);
  const double multiplier = 1000000000.0;
  _seconds = static_cast<int32_t>(integral);
  _nanoseconds = static_cast<uint32_t>(fractional * multiplier);
  if (_clock_publisher) {
    _clock_publisher->Write(_seconds, _nanoseconds);
    _clock_publisher->Publish();
  }
#if defined(WITH_ROS2_DEMO)
  _basic_publisher->SetData("Hello from Carla!");
  _basic_publisher->Publish();
#endif
}

void ROS2::RegisterSensor(
    void *actor, std::string ros_name, std::string frame_id, bool publish_tf,
    std::string ros_topic_name, PublisherQos qos) {
  // insert_or_assign so re-registering an actor with a new ros_name actually
  // updates the entry; unordered_map::insert would silently keep the stale
  // one.
  ActorRegistration reg;
  reg.ros_name = std::move(ros_name);
  reg.frame_id = std::move(frame_id);
  reg.ros_topic_name = std::move(ros_topic_name);
  reg.publish_tf = publish_tf;
  reg.qos = qos;
  _registrations.insert_or_assign(actor, std::move(reg));
}

void ROS2::UnregisterSensor(void *actor) {
  _publishers.erase(actor);
  _camera_publishers.erase(actor);
  _transforms.erase(actor);
  _actor_parents.erase(actor);
  _registrations.erase(actor);
}

void ROS2::RegisterVehicle(
    void *actor, std::string ros_name, std::string frame_id, ActorCallback callback,
    bool enable_ackermann_control) {
  _registrations.insert_or_assign(
      actor, ActorRegistration{.ros_name = ros_name, .frame_id = frame_id,
                               .ros_topic_name = {}, .publish_tf = true});

  // Idempotency: drop any prior subscribers / callbacks bound to this actor
  // so a re-registration does not accumulate duplicate DataReaders nor leave
  // the previous callback wired.
  _subscribers.erase(actor);
  _actor_callbacks.insert_or_assign(actor, std::move(callback));

  // The legacy CarlaEgoVehicleControlSubscriber::Init built its topic as
  // "rt/carla/" + [parent + "/"] + name + "/vehicle_control_cmd". With the
  // new template constructors the suffix is appended inside each subscriber,
  // so we hand them the base path only.
  const std::string base_topic_name = "rt/carla/" + ros_name;

  // The two control modes are mutually exclusive: a vehicle listens on either the
  // Ackermann topic or the direct VehicleControl topic, never both, so an Ackermann
  // message can never latch ApplyVehicleAckermannControl while plain VehicleControl
  // messages keep arriving on the other topic. Ackermann is opt-in per youtalk's review.
  if (enable_ackermann_control) {
    _subscribers.insert({actor, std::make_shared<AckermannControlSubscriber>(actor, base_topic_name, std::move(frame_id))});
  } else {
    _subscribers.insert({actor, std::make_shared<CarlaEgoVehicleControlSubscriber>(actor, base_topic_name, std::move(frame_id))});
  }

  // Register the per-vehicle data publishers
  VehiclePublishers vehicle_publishers;
  vehicle_publishers.odometry = std::make_shared<CarlaOdometryPublisher>(base_topic_name);
  vehicle_publishers.status = std::make_shared<CarlaEgoVehicleStatusPublisher>(base_topic_name);
  vehicle_publishers.info = std::make_shared<CarlaEgoVehicleInfoPublisher>(base_topic_name);
  _vehicle_publishers.insert({actor, std::move(vehicle_publishers)});
}

void ROS2::UnregisterVehicle(void *actor) {
  _subscribers.erase(actor);
  _actor_callbacks.erase(actor);
  _vehicle_publishers.erase(actor);
  UnregisterSensor(actor);
}

bool ROS2::IsVehicleRegistered(void *actor) const {
  return _vehicle_publishers.find(actor) != _vehicle_publishers.end();
}

void ROS2::AddActorParentRosName(void *actor, void *parent) {
  auto it = _actor_parents.find(actor);
  if (it != _actor_parents.end()) {
    it->second.push_back(parent);
  } else {
    _actor_parents.insert({actor, {parent}});
  }
}

void ROS2::AddBasicSubscriberCallback(
    [[maybe_unused]] void *actor,
    [[maybe_unused]] std::string ros_name,
    [[maybe_unused]] ActorMessageCallback callback) {
#if defined(WITH_ROS2_DEMO)
  _actor_message_callbacks.insert_or_assign(actor, std::move(callback));
  _basic_subscriber.reset();
  _basic_subscriber = std::make_shared<BasicSubscriber>(actor, ros_name.c_str());
  _basic_subscriber->Init();
#endif
}

void ROS2::RemoveBasicSubscriberCallback([[maybe_unused]] void *actor) {
#if defined(WITH_ROS2_DEMO)
  _basic_subscriber.reset();
  _actor_message_callbacks.erase(actor);
#endif
}

std::string ROS2::LookupRosName(void *actor) const {
  auto it = _registrations.find(actor);
  return it != _registrations.end() ? it->second.ros_name : std::string{};
}

std::string ROS2::LookupFrameId(void *actor) const {
  auto it = _registrations.find(actor);
  return it != _registrations.end() ? it->second.frame_id : std::string{};
}

std::string ROS2::BuildParentChain(void *actor) const {
  auto it = _actor_parents.find(actor);
  if (it == _actor_parents.end()) {
    return std::string{};
  }
  const std::string current_actor_name = LookupRosName(actor);
  std::string parent_name;
  for (auto *parent : it->second) {
    const std::string name = LookupRosName(parent);
    if (name.empty() || name == current_actor_name) {
      continue;
    }
    parent_name = name + '/' + parent_name;
  }
  if (!parent_name.empty() && parent_name.back() == '/') {
    parent_name.pop_back();
  }
  return parent_name;
}

std::string ROS2::BuildBaseTopicName(void *actor) const {
  auto it = _registrations.find(actor);
  if (it == _registrations.end()) {
    return std::string{};
  }
  // Verbatim override: the runner supplied the exact ROS topic. Prepend only
  // the DDS wire prefix "rt" (the middleware maps "rt/<x>" <-> ROS "/<x>").
  // Skip the "carla/" segment, the parent chain, AND the per-type suffix so
  // the Autoware topic name is emitted exactly as configured.
  if (!it->second.ros_topic_name.empty()) {
    const std::string &t = it->second.ros_topic_name;
    return t.front() == '/' ? "rt" + t : "rt/" + t;
  }
  const std::string &ros_name = it->second.ros_name;
  if (ros_name.empty()) {
    return std::string{};
  }
  const std::string parent_chain = BuildParentChain(actor);
  std::string base_topic_name = "rt/carla/";
  if (!parent_chain.empty()) {
    base_topic_name += parent_chain + "/";
  }
  base_topic_name += ros_name;
  return base_topic_name;
}

void ROS2::ResolveAutoStreamSuffix(
    void *actor,
    const std::string &prefix,
    carla::streaming::detail::stream_id_type id) {
  auto it = _registrations.find(actor);
  if (it == _registrations.end()) {
    return;
  }
  const std::string placeholder = prefix + "__";
  if (it->second.ros_name != placeholder) {
    return;
  }
  std::string resolved = prefix + std::to_string(id);
  it->second.ros_name = resolved;
  if (it->second.frame_id == placeholder) {
    it->second.frame_id = std::move(resolved);
  }
}

template <typename CameraT>
std::shared_ptr<CarlaCameraPublisher> ROS2::GetOrCreateCameraSensor(
    carla::streaming::detail::stream_id_type id,
    void *actor,
    const std::string &default_prefix) {
  auto it_camera = _camera_publishers.find(actor);
  if (it_camera != _camera_publishers.end()) {
    // Enforce the one-actor-one-camera-type invariant on the cache-hit path.
    // RGB/Depth/SS/IS/Normals all alias CarlaRGBCameraPublisher (shared BGRA
    // passthrough), so a hit across those types casts cleanly and is expected.
    // Only an RGB <-> OpticalFlow mismatch fails the cast: that would route
    // optical-flow float bytes through an RGB publisher (or vice versa). Surface
    // it and skip the sample instead of letting the first-created type silently
    // win and corrupt the published image.
    auto typed = std::dynamic_pointer_cast<CameraT>(it_camera->second);
    if (typed == nullptr) {
      log_error(
          "ROS2 camera publisher type mismatch for actor", actor,
          "- the actor was dispatched as two different camera types; ignoring this sample.");
      return nullptr;
    }
    return typed;
  }

  ResolveAutoStreamSuffix(actor, default_prefix, id);
  const std::string base_topic_name = BuildBaseTopicName(actor);
  const std::string frame_id = LookupFrameId(actor);

  auto new_publisher = std::make_shared<CameraT>(base_topic_name, frame_id);
  _camera_publishers.insert({actor, new_publisher});
  return new_publisher;
}

std::shared_ptr<BasePublisher> ROS2::GetOrCreateSensor(
    int type, carla::streaming::detail::stream_id_type id, void *actor) {
  auto it_publishers = _publishers.find(actor);
  if (it_publishers != _publishers.end()) {
    return it_publishers->second;
  }

  // Resolve auto-naming "prefix__" -> "prefix<stream_id>" before computing the
  // topic name. Each enum case names its own prefix so the resolved ros_name
  // stays stable across ticks.
  auto resolve = [this, actor, id](const std::string &prefix) {
    ResolveAutoStreamSuffix(actor, prefix, id);
  };

  std::shared_ptr<BasePublisher> publisher;
  switch (type) {
    case ESensors::CollisionSensor: {
      resolve("collision");
      publisher = std::make_shared<CarlaCollisionPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor));
      break;
    }
    case ESensors::DVSCamera: {
      resolve("dvs");
      publisher = std::make_shared<CarlaDVSCameraPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor));
      break;
    }
    case ESensors::GnssSensor: {
      resolve("gnss");
      publisher = std::make_shared<CarlaGNSSPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor));
      break;
    }
    case ESensors::InertialMeasurementUnit: {
      resolve("imu");
      publisher = std::make_shared<CarlaIMUPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor));
      break;
    }
    case ESensors::Radar: {
      resolve("radar");
      publisher = std::make_shared<CarlaRadarPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor));
      break;
    }
    case ESensors::RayCastSemanticLidar: {
      resolve("ray_cast_semantic");
      publisher = std::make_shared<CarlaSemanticLidarPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor));
      break;
    }
    case ESensors::RayCastLidar: {
      // Both ray-cast and HSS lidars dispatch here; resolve either placeholder.
      // Skip auto-naming resolution when the sensor has a verbatim
      // ros_topic_name override: BuildBaseTopicName never consults ros_name in
      // that case, so resolving the "prefix__" placeholder would be pointless
      // mutation. has_override also tells the publisher to skip the
      // "/point_cloud" suffix append (see CarlaPointCloudPublisher::Init) so
      // the override topic is emitted exactly as configured.
      const auto reg_it = _registrations.find(actor);
      const bool has_override =
          reg_it != _registrations.end() && !reg_it->second.ros_topic_name.empty();
      if (!has_override) {
        resolve("ray_cast");
        resolve("hss_lidar");
      }
      // Per-sensor QoS (reliability/durability/history depth), parsed from
      // the ros2_qos_* blueprint attributes in ActorDispatcher::RegisterActor
      // and threaded through RegisterSensor. Falls back to SensorData()
      // (best_effort/volatile/depth1) if the actor is somehow unregistered by
      // the time its first sample arrives — the same pre-QoS-support default
      // every CarlaPointCloudPublisher subclass had, not the plain Reliable
      // struct default, so this edge case cannot silently upgrade a lidar to
      // a subscriber-blocking writer.
      const PublisherQos qos = reg_it != _registrations.end() ? reg_it->second.qos : PublisherQos::SensorData();
      publisher = std::make_shared<CarlaLidarPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor), has_override, qos);
      break;
    }
    case ESensors::LaneInvasionSensor:
    case ESensors::ObstacleDetectionSensor:
    case ESensors::RssSensor:
    case ESensors::WorldObserver:
    case ESensors::CameraGBufferUint8:
    case ESensors::CameraGBufferFloat:
      // Sensors without a publisher on ue5-dev today; the dispatch in
      // ProcessDataFrom* logs and exits cleanly.
      return nullptr;
    default:
      log_error("ROS2::GetOrCreateSensor: unknown sensor type", type);
      return nullptr;
  }

  if (publisher) {
    _publishers.insert({actor, publisher});
  }
  return publisher;
}

std::shared_ptr<CarlaTransformPublisher> ROS2::GetOrCreateTransformPublisher(void *actor) {
  auto it = _transforms.find(actor);
  if (it != _transforms.end()) {
    return it->second;
  }
  auto registration_it = _registrations.find(actor);
  if (registration_it == _registrations.end() || !registration_it->second.publish_tf) {
    return nullptr;
  }
  auto transform = std::make_shared<CarlaTransformPublisher>();
  _transforms.insert({actor, transform});
  return transform;
}

namespace {

// Builds the parent_frame_id for TF: top-level actors broadcast against
// "map"; child actors broadcast against their direct parent's frame_id.
std::string ParentFrameOrMap(const std::string &parent_chain) {
  return parent_chain.empty() ? std::string{"map"} : parent_chain;
}

}  // namespace

void ROS2::ProcessDataFromCamera(
    uint64_t sensor_type,
    carla::streaming::detail::stream_id_type stream_id,
    const carla::geom::Transform sensor_transform,
    int W, int H, float Fov,
    const carla::SharedBufferView buffer,
    void *actor) {
  // Image dimensions + FOV are now read straight from ImageSerializer's
  // per-frame header inside the camera publisher's WriteCameraInfo call;
  // the W/H/Fov arguments survive for ABI compatibility with the
  // Unreal-side dispatcher.
  (void)W;
  (void)H;
  (void)Fov;

  std::shared_ptr<CarlaCameraPublisher> publisher;
  switch (sensor_type) {
    case ESensors::SceneCaptureCamera:
      publisher = GetOrCreateCameraSensor<CarlaRGBCameraPublisher>(stream_id, actor, "rgb");
      break;
    case ESensors::DepthCamera:
      publisher = GetOrCreateCameraSensor<CarlaDepthCameraPublisher>(stream_id, actor, "depth");
      break;
    case ESensors::NormalsCamera:
      publisher = GetOrCreateCameraSensor<CarlaNormalsCameraPublisher>(stream_id, actor, "normals");
      break;
    case ESensors::SemanticSegmentationCamera:
      publisher = GetOrCreateCameraSensor<CarlaSSCameraPublisher>(stream_id, actor, "semantic_segmentation");
      break;
    case ESensors::InstanceSegmentationCamera:
      publisher = GetOrCreateCameraSensor<CarlaISCameraPublisher>(stream_id, actor, "instance_segmentation");
      break;
    case ESensors::OpticalFlowCamera:
      publisher = GetOrCreateCameraSensor<CarlaOpticalFlowCameraPublisher>(
          stream_id, actor, "optical_flow");
      break;
    case ESensors::CollisionSensor:
    case ESensors::RssSensor:
    case ESensors::WorldObserver:
    case ESensors::CameraGBufferUint8:
    case ESensors::CameraGBufferFloat:
    default:
      log_info(
          "Sensor to ROS data: frame.", _frame, "sensor.", sensor_type, "stream.", stream_id,
          "buffer.", buffer->size());
      return;
  }

  if (publisher) {
    const auto *header_ptr = buffer->data();
    if (!header_ptr) {
      return;
    }
    if (sensor_type == ESensors::OpticalFlowCamera) {
      const auto *header = reinterpret_cast<
          const carla::sensor::s11n::OpticalFlowImageSerializer::ImageHeader *>(header_ptr);
      publisher->WriteCameraInfo(
          _seconds, _nanoseconds, 0, 0, header->height, header->width, header->fov_angle, true);
      publisher->WriteImage(
          _seconds, _nanoseconds, header->height, header->width,
          buffer->data() + carla::sensor::s11n::OpticalFlowImageSerializer::header_offset);
    } else {
      const auto *header = reinterpret_cast<
          const carla::sensor::s11n::ImageSerializer::ImageHeader *>(header_ptr);
      publisher->WriteCameraInfo(
          _seconds, _nanoseconds, 0, 0, header->height, header->width, header->fov_angle, true);
      publisher->WriteImage(
          _seconds, _nanoseconds, header->height, header->width,
          buffer->data() + carla::sensor::s11n::ImageSerializer::header_offset);
    }
    publisher->Publish();
  }

  if (auto transform_publisher = GetOrCreateTransformPublisher(actor)) {
    transform_publisher->Write(
        _seconds, _nanoseconds,
        ParentFrameOrMap(BuildParentChain(actor)),
        LookupFrameId(actor),
        sensor_transform.location.x, sensor_transform.location.y, sensor_transform.location.z,
        sensor_transform.rotation.pitch, sensor_transform.rotation.yaw, sensor_transform.rotation.roll);
    transform_publisher->Publish();
  }
}

void ROS2::ProcessDataFromGNSS(
    uint64_t /*sensor_type*/,
    carla::streaming::detail::stream_id_type stream_id,
    const carla::geom::Transform sensor_transform,
    const carla::geom::GeoLocation &data,
    void *actor) {
  if (auto base = GetOrCreateSensor(ESensors::GnssSensor, stream_id, actor)) {
    auto publisher = std::dynamic_pointer_cast<CarlaGNSSPublisher>(base);
    publisher->Write(_seconds, _nanoseconds, data.latitude, data.longitude, data.altitude);
    publisher->Publish();
  }
  if (auto transform_publisher = GetOrCreateTransformPublisher(actor)) {
    transform_publisher->Write(
        _seconds, _nanoseconds,
        ParentFrameOrMap(BuildParentChain(actor)),
        LookupFrameId(actor),
        sensor_transform.location.x, sensor_transform.location.y, sensor_transform.location.z,
        sensor_transform.rotation.pitch, sensor_transform.rotation.yaw, sensor_transform.rotation.roll);
    transform_publisher->Publish();
  }
}

void ROS2::ProcessDataFromIMU(
    uint64_t /*sensor_type*/,
    carla::streaming::detail::stream_id_type stream_id,
    const carla::geom::Transform sensor_transform,
    carla::geom::Vector3D accelerometer,
    carla::geom::Vector3D gyroscope,
    float compass,
    void *actor) {
  if (auto base = GetOrCreateSensor(ESensors::InertialMeasurementUnit, stream_id, actor)) {
    auto publisher = std::dynamic_pointer_cast<CarlaIMUPublisher>(base);
    publisher->Write(
        _seconds, _nanoseconds,
        accelerometer.x, accelerometer.y, accelerometer.z,
        gyroscope.x, gyroscope.y, gyroscope.z,
        compass);
    publisher->Publish();
  }
  if (auto transform_publisher = GetOrCreateTransformPublisher(actor)) {
    transform_publisher->Write(
        _seconds, _nanoseconds,
        ParentFrameOrMap(BuildParentChain(actor)),
        LookupFrameId(actor),
        sensor_transform.location.x, sensor_transform.location.y, sensor_transform.location.z,
        sensor_transform.rotation.pitch, sensor_transform.rotation.yaw, sensor_transform.rotation.roll);
    transform_publisher->Publish();
  }
}

void ROS2::ProcessDataFromDVS(
    uint64_t /*sensor_type*/,
    carla::streaming::detail::stream_id_type stream_id,
    const carla::geom::Transform sensor_transform,
    const carla::SharedBufferView buffer,
    int /*W*/, int /*H*/, float /*Fov*/,
    void *actor) {
  if (auto base = GetOrCreateSensor(ESensors::DVSCamera, stream_id, actor)) {
    auto publisher = std::dynamic_pointer_cast<CarlaDVSCameraPublisher>(base);
    const auto *header = reinterpret_cast<
        const carla::sensor::s11n::ImageSerializer::ImageHeader *>(buffer->data());
    if (!header) {
      return;
    }
    constexpr std::size_t header_offset =
        carla::sensor::s11n::ImageSerializer::header_offset;
    constexpr std::size_t event_size = sizeof(carla::sensor::data::DVSEvent);
    const std::size_t event_count = (buffer->size() - header_offset) / event_size;
    const std::uint8_t *event_bytes = buffer->data() + header_offset;

    publisher->WriteCameraInfo(
        _seconds, _nanoseconds, 0, 0, header->height, header->width, header->fov_angle, true);
    publisher->WriteImage(
        _seconds, _nanoseconds, header->height, header->width,
        event_count, event_bytes, event_size);
    publisher->WritePointCloud(
        _seconds, _nanoseconds, 1, static_cast<std::uint32_t>(event_count), event_bytes);
    publisher->Publish();
  }
  if (auto transform_publisher = GetOrCreateTransformPublisher(actor)) {
    transform_publisher->Write(
        _seconds, _nanoseconds,
        ParentFrameOrMap(BuildParentChain(actor)),
        LookupFrameId(actor),
        sensor_transform.location.x, sensor_transform.location.y, sensor_transform.location.z,
        sensor_transform.rotation.pitch, sensor_transform.rotation.yaw, sensor_transform.rotation.roll);
    transform_publisher->Publish();
  }
}

void ROS2::ProcessDataFromLidar(
    uint64_t /*sensor_type*/,
    carla::streaming::detail::stream_id_type stream_id,
    const carla::geom::Transform sensor_transform,
    carla::sensor::data::LidarData &data,
    void *actor) {
  if (auto base = GetOrCreateSensor(ESensors::RayCastLidar, stream_id, actor)) {
    auto publisher = std::dynamic_pointer_cast<CarlaLidarPublisher>(base);
    // The lidar returns a flat list of floats rather than structured detection
    // points. Each detection is 4 floats: x, y, z, intensity. Divide the total
    // float count by 4 to recover the number of detections.
    const auto width = static_cast<std::uint32_t>(data._points.size() / 4u);
    publisher->WritePointCloud(
        _seconds, _nanoseconds, 1u, width,
        reinterpret_cast<const std::uint8_t *>(data._points.data()));
    publisher->Publish();
  }
  if (auto transform_publisher = GetOrCreateTransformPublisher(actor)) {
    transform_publisher->Write(
        _seconds, _nanoseconds,
        ParentFrameOrMap(BuildParentChain(actor)),
        LookupFrameId(actor),
        sensor_transform.location.x, sensor_transform.location.y, sensor_transform.location.z,
        sensor_transform.rotation.pitch, sensor_transform.rotation.yaw, sensor_transform.rotation.roll);
    transform_publisher->Publish();
  }
}

void ROS2::ProcessDataFromSemanticLidar(
    uint64_t /*sensor_type*/,
    carla::streaming::detail::stream_id_type stream_id,
    const carla::geom::Transform sensor_transform,
    carla::sensor::data::SemanticLidarData &data,
    void *actor) {
  if (auto base = GetOrCreateSensor(ESensors::RayCastSemanticLidar, stream_id, actor)) {
    auto publisher = std::dynamic_pointer_cast<CarlaSemanticLidarPublisher>(base);
    const auto width = static_cast<std::uint32_t>(data._ser_points.size());
    publisher->WritePointCloud(
        _seconds, _nanoseconds, 1u, width,
        reinterpret_cast<const std::uint8_t *>(data._ser_points.data()));
    publisher->Publish();
  }
  if (auto transform_publisher = GetOrCreateTransformPublisher(actor)) {
    transform_publisher->Write(
        _seconds, _nanoseconds,
        ParentFrameOrMap(BuildParentChain(actor)),
        LookupFrameId(actor),
        sensor_transform.location.x, sensor_transform.location.y, sensor_transform.location.z,
        sensor_transform.rotation.pitch, sensor_transform.rotation.yaw, sensor_transform.rotation.roll);
    transform_publisher->Publish();
  }
}

void ROS2::ProcessDataFromRadar(
    uint64_t /*sensor_type*/,
    carla::streaming::detail::stream_id_type stream_id,
    const carla::geom::Transform sensor_transform,
    const carla::sensor::data::RadarData &data,
    void *actor) {
  if (auto base = GetOrCreateSensor(ESensors::Radar, stream_id, actor)) {
    auto publisher = std::dynamic_pointer_cast<CarlaRadarPublisher>(base);
    const auto width = static_cast<std::uint32_t>(data.GetDetectionCount());
    publisher->WritePointCloud(
        _seconds, _nanoseconds, 1u, width,
        reinterpret_cast<const std::uint8_t *>(data._detections.data()));
    publisher->Publish();
  }
  if (auto transform_publisher = GetOrCreateTransformPublisher(actor)) {
    transform_publisher->Write(
        _seconds, _nanoseconds,
        ParentFrameOrMap(BuildParentChain(actor)),
        LookupFrameId(actor),
        sensor_transform.location.x, sensor_transform.location.y, sensor_transform.location.z,
        sensor_transform.rotation.pitch, sensor_transform.rotation.yaw, sensor_transform.rotation.roll);
    transform_publisher->Publish();
  }
}

void ROS2::ProcessDataFromObstacleDetection(
    uint64_t sensor_type,
    carla::streaming::detail::stream_id_type stream_id,
    const carla::geom::Transform /*sensor_transform*/,
    AActor * /*first_actor*/,
    AActor * /*second_actor*/,
    float distance,
    void * /*actor*/) {
  log_info(
      "Sensor ObstacleDetector to ROS data: frame.", _frame, "sensor.", sensor_type,
      "stream.", stream_id, "distance.", distance);
}

void ROS2::ProcessDataFromCollisionSensor(
    uint64_t /*sensor_type*/,
    carla::streaming::detail::stream_id_type stream_id,
    const carla::geom::Transform sensor_transform,
    uint32_t other_actor,
    carla::geom::Vector3D impulse,
    void *actor) {
  if (auto base = GetOrCreateSensor(ESensors::CollisionSensor, stream_id, actor)) {
    auto publisher = std::dynamic_pointer_cast<CarlaCollisionPublisher>(base);
    publisher->Write(_seconds, _nanoseconds, other_actor, impulse.x, impulse.y, impulse.z);
    publisher->Publish();
  }
  if (auto transform_publisher = GetOrCreateTransformPublisher(actor)) {
    transform_publisher->Write(
        _seconds, _nanoseconds,
        ParentFrameOrMap(BuildParentChain(actor)),
        LookupFrameId(actor),
        sensor_transform.location.x, sensor_transform.location.y, sensor_transform.location.z,
        sensor_transform.rotation.pitch, sensor_transform.rotation.yaw, sensor_transform.rotation.roll);
    transform_publisher->Publish();
  }
}

void ROS2::ProcessDataFromMap(const std::string &open_drive) {
  if (!_enabled) {
    return;
  }
  if (open_drive.empty()) {
    // Reached once per episode start, never per frame, so logging
    // unconditionally cannot flood the output.
    log_warning("ROS2: empty OpenDRIVE description, skipping map publish");
    return;
  }
  if (!_map_publisher) {
    _map_publisher = std::make_shared<CarlaMapPublisher>();
  }
  _map_publisher->Write(open_drive);
  _map_publisher->Publish();
}

void ROS2::ProcessDataFromVehicle(
    void *actor,
    const carla::geom::Transform vehicle_transform,
    carla::geom::Vector3D velocity,
    carla::geom::Vector3D angular_velocity,
    float delta_seconds,
    const carla::rpc::VehicleControl &control) {
  if (!_enabled) {
    return;
  }
  auto it = _vehicle_publishers.find(actor);
  if (it == _vehicle_publishers.end()) {
    return;
  }
  const std::string frame_id = LookupFrameId(actor);

  // The UE -> ROS coordinate conversion is done here (server side) so the
  // per-vehicle publishers in the carla-ros2-native library stay free of
  // carla::geom / carla::rpc, which pull MsgPack + Boost via carla/MsgPack.h.
  const msg::Quaternion orientation = ue_rotation_to_ros_quaternion(vehicle_transform.rotation);

  // Odometry: pose in the odom frame plus body-frame twist.
  msg::Vector3 position;
  position.x = vehicle_transform.location.x;
  position.y = -vehicle_transform.location.y;
  position.z = vehicle_transform.location.z;
  const msg::Vector3 body_velocity =
      ue_world_velocity_to_ros_body_velocity(velocity, vehicle_transform.rotation);
  const msg::Vector3 ros_angular_velocity = ue_angular_velocity_to_ros(angular_velocity);
  it->second.odometry->Write(
      _seconds, _nanoseconds, "odom", frame_id, position, orientation, body_velocity,
      ros_angular_velocity);
  it->second.odometry->Publish();

  // Vehicle status: speed + acceleration from the world-frame velocity and the
  // echoed control command.
  const msg::Vector3 ros_velocity = ue_vector_to_ros_vector(velocity);
  msg::CarlaEgoVehicleControl ros_control;
  ros_control.throttle = control.throttle;
  ros_control.steer = control.steer;
  ros_control.brake = control.brake;
  ros_control.hand_brake = control.hand_brake;
  ros_control.reverse = control.reverse;
  ros_control.gear = control.gear;
  ros_control.manual_gear_shift = control.manual_gear_shift;
  it->second.status->Write(
      _seconds, _nanoseconds, "map", orientation, ros_velocity, delta_seconds, ros_control);
  it->second.status->Publish();
}

void ROS2::ProcessVehicleInfo(
    void *actor,
    uint32_t id,
    const std::string &type_id,
    const std::string &role_name,
    const carla::geom::Transform vehicle_transform,
    const carla::rpc::VehiclePhysicsControl &physics_control) {
  if (!_enabled) {
    return;
  }
  auto it = _vehicle_publishers.find(actor);
  if (it == _vehicle_publishers.end()) {
    return;
  }
  // Build the ROS message here (server side) so CarlaEgoVehicleInfoPublisher
  // in the carla-ros2-native library stays free of carla::geom / carla::rpc.
  msg::CarlaEgoVehicleInfo info;
  info.id = id;
  info.type = type_id;
  info.rolename = role_name;

  // CarlaEgoVehicleInfo mirrors the ros-carla-msgs description, whose physics
  // fields follow the UE4 PhysX vehicle model. UE5 uses the Chaos model with a
  // different parameter set, so the fields below are a best-effort mapping:
  // direct where an equivalent exists, and left at zero where Chaos has no
  // counterpart (tire damping, throttle/clutch damping rates, clutch strength).
  info.wheels.reserve(physics_control.wheels.size());
  for (const auto &wheel : physics_control.wheels) {
    msg::CarlaEgoVehicleInfoWheel wheel_info;
    // Chaos exposes a friction multiplier rather than a raw tire-friction
    // coefficient; it is the closest available analogue.
    wheel_info.tire_friction = wheel.friction_force_multiplier;
    wheel_info.damping_rate = 0.0f;  // no Chaos wheel-damping equivalent
    wheel_info.max_steer_angle = wheel.max_steer_angle * UE_DEG_TO_RAD;
    wheel_info.radius = wheel.wheel_radius;
    wheel_info.max_brake_torque = wheel.max_brake_torque;
    wheel_info.max_handbrake_torque = wheel.max_hand_brake_torque;

    // Wheel locations arrive as world coordinates in centimeters; express
    // them in the vehicle frame in meters, then flip to right-handed.
    geom::Vector3D wheel_position{
        wheel.location.x / 100.0f,
        wheel.location.y / 100.0f,
        wheel.location.z / 100.0f};
    vehicle_transform.InverseTransformPoint(wheel_position);
    wheel_info.position.x = wheel_position.x;
    wheel_info.position.y = -wheel_position.y;
    wheel_info.position.z = wheel_position.z;

    info.wheels.push_back(wheel_info);
  }

  info.max_rpm = physics_control.max_rpm;
  info.moi = physics_control.rev_up_moi;  // closest Chaos engine-inertia analogue
  info.damping_rate_full_throttle = 0.0f;  // no Chaos equivalent
  info.damping_rate_zero_throttle_clutch_engaged = 0.0f;  // no Chaos equivalent
  info.damping_rate_zero_throttle_clutch_disengaged = 0.0f;  // no Chaos equivalent
  info.use_gear_autobox = physics_control.use_automatic_gears;
  info.gear_switch_time = physics_control.gear_change_time;
  info.clutch_strength = 0.0f;  // no Chaos clutch model
  info.mass = physics_control.mass;
  info.drag_coefficient = physics_control.drag_coefficient;
  info.center_of_mass.x = physics_control.center_of_mass.x;
  info.center_of_mass.y = physics_control.center_of_mass.y;
  info.center_of_mass.z = physics_control.center_of_mass.z;

  it->second.info->Write(info);
  it->second.info->Publish();
}

void ROS2::Shutdown() {
  for (auto &element : _publishers) {
    element.second.reset();
  }
  for (auto &element : _transforms) {
    element.second.reset();
  }
  for (auto &element : _camera_publishers) {
    element.second.reset();
  }
  _publishers.clear();
  _transforms.clear();
  _camera_publishers.clear();
  _vehicle_publishers.clear();
  _map_publisher.reset();
  _subscribers.clear();
  _actor_callbacks.clear();
  _registrations.clear();
  _actor_parents.clear();
  _clock_publisher.reset();
  _enabled = false;
#if defined(WITH_ROS2_DEMO)
  _basic_publisher.reset();
  _basic_subscriber.reset();
#endif
}

}  // namespace ros2
}  // namespace carla
