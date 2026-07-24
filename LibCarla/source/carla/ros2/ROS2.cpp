// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "carla/Logging.h"
#include "carla/ros2/ROS2.h"
#include "carla/ros2/extension/CarlaRos2Extension.h"
#include "carla/ros2/extension/ExtensionTransform.h"
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
#include "publishers/ExtendedLidarPoint.h"
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

#include <algorithm>
#include <cmath>
#include <cstddef>
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
  // Apply any Ackermann commands the out-of-tree extension staged since the last
  // frame. Draining here (game thread) after the subscriber callbacks funnels
  // extension-sourced control through the same _actor_callbacks visit as the
  // native subscriber, so ApplyVehicleAckermannControl is reached from exactly
  // one place regardless of the source.
  DrainExtensionPendingCommands();
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
    std::string ros_topic_name, PublisherQos qos, bool extended_lidar) {
  // insert_or_assign so re-registering an actor with a new ros_name actually
  // updates the entry; unordered_map::insert would silently keep the stale
  // one.
  ActorRegistration reg;
  reg.ros_name = std::move(ros_name);
  reg.frame_id = std::move(frame_id);
  reg.ros_topic_name = std::move(ros_topic_name);
  reg.publish_tf = publish_tf;
  reg.qos = qos;
  reg.extended_lidar = extended_lidar;
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
    void *actor, uint32_t actor_id, std::string ros_name, std::string frame_id,
    ActorCallback callback, bool enable_ackermann_control) {
  _registrations.insert_or_assign(
      actor, ActorRegistration{.ros_name = ros_name, .frame_id = frame_id,
                               .ros_topic_name = {}, .publish_tf = true});

  // Idempotency: drop any prior subscribers / callbacks bound to this actor
  // so a re-registration does not accumulate duplicate DataReaders nor leave
  // the previous callback wired.
  _subscribers.erase(actor);
  _actor_callbacks.insert_or_assign(actor, std::move(callback));

  // Extension-seam id bookkeeping: record both directions of the id<->actor*
  // mapping and mark this actor as the ego. This is what turns LookupActorId /
  // GetEgoActorIdForExtension / LookupRosNameById live (they return the
  // zero/empty sentinel until a vehicle registers) and lets ApplyExtension
  // Ackermann resolve an actor id back to its actor*. insert_or_assign so a
  // re-registration of the same actor refreshes rather than duplicates. Locked
  // against a concurrent foreign-thread read (see _actor_maps_mutex).
  {
    std::lock_guard<std::mutex> lock(_actor_maps_mutex);
    _actor_by_id.insert_or_assign(actor_id, actor);
    _id_by_actor.insert_or_assign(actor, actor_id);
    _ego_actor_id = actor_id;
  }

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
  // Tear down the extension-seam id bookkeeping this actor owned. Clear
  // _ego_actor_id only if it still points at THIS actor, so unregistering a
  // non-ego actor cannot blank a live ego id. Locked against a concurrent
  // foreign-thread read (see _actor_maps_mutex).
  {
    std::lock_guard<std::mutex> lock(_actor_maps_mutex);
    auto id_it = _id_by_actor.find(actor);
    if (id_it != _id_by_actor.end()) {
      if (_ego_actor_id == id_it->second) {
        _ego_actor_id = 0;
      }
      _actor_by_id.erase(id_it->second);
      _id_by_actor.erase(id_it);
    }
  }
  // Drop any staged-but-undrained extension Ackermann command for this actor
  // too: ApplyExtensionAckermann can stage into _ext_pending_cmds from the
  // extension's subscriber-listener thread right up until this despawn, and
  // without this erase a stale entry would survive keyed on a now-dangling
  // actor pointer, then get applied against whatever unrelated actor is
  // allocated at that same address later (or found and visited via a stale
  // _actor_callbacks-adjacent lookup). Locked against the same producer/
  // consumer race DrainExtensionPendingCommands guards against.
  {
    std::lock_guard<std::mutex> lock(_ext_pending_cmds_mutex);
    _ext_pending_cmds.erase(actor);
  }
  UnregisterSensor(actor);
}

bool ROS2::IsVehicleRegistered(void *actor) const {
  return _vehicle_publishers.find(actor) != _vehicle_publishers.end();
}

// ---------------------------------------------------------------------------
// Out-of-tree ROS 2 extension seam (host side). These are the concrete targets
// the CarlaRos2Host vtable slots (built in ExtensionHost.cpp) route through.

void ROS2::RegisterExtensionObserver(int kind, CarlaRos2SensorObserver cb, void *user) {
  if (cb == nullptr) {
    return;  // a null callback would crash the synchronous dispatch loop
  }
  // Idempotency: registering the exact same (kind, cb, user) triple twice would
  // dispatch the sample into that observer twice per frame. Skip the duplicate
  // and warn — a re-Load of the same extension (or a double register_observer
  // in on_init) is the likely cause, and silent double-dispatch is a subtle bug.
  for (const auto &o : _ext_observers) {
    if (o.kind == kind && o.cb == cb && o.user == user) {
      log_warning("ROS2: extension observer already registered for kind", kind,
                  "- ignoring duplicate registration");
      return;
    }
  }
  _ext_observers.push_back(ExtObserver{kind, cb, user});
}

void ROS2::ClearExtensionObservers() {
  _ext_observers.clear();
}

uint32_t ROS2::GetEgoActorIdForExtension() const {
  // The hero vehicle is the single RegisterVehicle actor; RegisterVehicle sets
  // _ego_actor_id. Returns 0 ("none registered") until then. Reached from a
  // foreign thread via the host vtable, so lock (see _actor_maps_mutex).
  std::lock_guard<std::mutex> lock(_actor_maps_mutex);
  return _ego_actor_id;
}

const char *ROS2::GetActorRosNameForExtension(uint32_t actor_id) const {
  // thread_local so the returned char* stays valid until this thread's next
  // call, without the const method mutating shared ROS2 state.
  static thread_local std::string name;
  name = LookupRosNameById(actor_id);
  return name.c_str();
}

uint32_t ROS2::LookupActorId(void *actor) const {
  std::lock_guard<std::mutex> lock(_actor_maps_mutex);
  auto it = _id_by_actor.find(actor);
  return it == _id_by_actor.end() ? 0u : it->second;
}

std::string ROS2::LookupRosNameById(uint32_t actor_id) const {
  // Resolve id -> actor* under the maps lock and copy the pointer out; release
  // the lock before LookupRosName (which reads a different, game-thread-owned
  // map) so we never hold _actor_maps_mutex across unrelated work.
  void *actor = nullptr;
  {
    std::lock_guard<std::mutex> lock(_actor_maps_mutex);
    auto it = _actor_by_id.find(actor_id);
    if (it == _actor_by_id.end()) {
      return std::string{};
    }
    actor = it->second;
  }
  return LookupRosName(actor);
}

void ROS2::DispatchVehicleStatusView(
    uint32_t actor_id, const char *ros_name, const CarlaRos2Transform &transform,
    double velocity_mps, double lateral_velocity_mps, double yaw_rate_rps,
    double steering_tire_angle_rad, int32_t gear, double sim_time_s) {
  // Single point that fills the VEHICLE_STATUS view + sample and fans it out, so
  // the live ProcessDataFromVehicle tap and the test driver can never drift. The
  // view and its ros_name buffer are valid ONLY for this synchronous dispatch
  // (see the observer contract in CarlaRos2Extension.h).
  CarlaRos2VehicleStatusView view = {};
  view.actor_id = actor_id;
  view.ros_name = ros_name;
  view.transform = transform;
  view.velocity_mps = velocity_mps;
  view.lateral_velocity_mps = lateral_velocity_mps;
  view.yaw_rate_rps = yaw_rate_rps;
  view.steering_tire_angle_rad = steering_tire_angle_rad;
  view.gear = gear;
  view.sim_time_s = sim_time_s;
  CarlaRos2SensorSample sample = {};
  sample.kind = CARLA_ROS2_SENSOR_VEHICLE_STATUS;
  sample.actor_id = actor_id;
  sample.ros_name = ros_name;
  sample.data = &view;
  sample.data_size = sizeof(view);
  for (auto &o : _ext_observers) {
    if (o.kind == CARLA_ROS2_SENSOR_VEHICLE_STATUS) {
      o.cb(o.user, &sample);
    }
  }
}

void ROS2::DispatchVehicleStatusObserversForTest(
    uint32_t actor_id, const char *ros_name, double velocity_mps,
    double steer_rad, int32_t gear, double sim_t) {
  // Thin wrapper over the shared fill/dispatch helper (zero transform + zero
  // lateral/yaw, which the live tap computes from real kinematics).
  DispatchVehicleStatusView(actor_id, ros_name, CarlaRos2Transform{},
                            velocity_mps, /*lateral_velocity_mps=*/0.0,
                            /*yaw_rate_rps=*/0.0, steer_rad, gear, sim_t);
}

void ROS2::ApplyExtensionAckermann(uint32_t actor_id, const AckermannControl &cmd) {
  // Resolve id -> actor* under the maps lock (this can run on the extension's
  // subscriber-listener thread, concurrently with a game-thread register/
  // unregister), copy the pointer out, and release the lock before staging.
  void *actor = nullptr;
  {
    std::lock_guard<std::mutex> lock(_actor_maps_mutex);
    auto it = _actor_by_id.find(actor_id);
    if (it != _actor_by_id.end()) {
      actor = it->second;
    }
  }
  if (actor == nullptr) {
    // Unknown actor: drop. The extension may address an id that has since been
    // unregistered (or was never the ego); silently ignoring is safer than
    // fabricating a target. Logged outside the lock; loud in debug.
    log_debug("ROS2: ApplyExtensionAckermann for unknown actor id", actor_id,
              "- dropping command");
    return;
  }
  // Stage only — do NOT invoke _actor_callbacks here. The actual actuation
  // (ActorROS2Handler -> ApplyVehicleAckermannControl) must run on the game
  // thread, which DrainExtensionPendingCommands does from SetFrame. Last-wins
  // per actor: a second command for the same actor before the next drain
  // overwrites the first (single-slot semantics, bounded growth). The lock
  // guards the map against a concurrent game-thread drain.
  std::lock_guard<std::mutex> lock(_ext_pending_cmds_mutex);
  _ext_pending_cmds[actor] = ROS2CallbackData{cmd};
}

void ROS2::DrainExtensionPendingCommands() {
  // Swap the staged commands out under the lock, then invoke the callbacks with
  // the lock released: the callbacks run UE actuation code (game thread) and a
  // producer on the listener thread must never block on it, nor may that UE code
  // re-enter the queue under the held lock.
  std::unordered_map<void *, ROS2CallbackData> pending;
  {
    std::lock_guard<std::mutex> lock(_ext_pending_cmds_mutex);
    pending.swap(_ext_pending_cmds);
  }
  for (auto &entry : pending) {
    auto cb = _actor_callbacks.find(entry.first);
    if (cb != _actor_callbacks.end()) {
      cb->second(entry.first, entry.second);
    }
  }
}

void ROS2::RegisterVehicleCallbackForTest(
    void *actor, uint32_t actor_id, ActorCallback cb) {
  _actor_callbacks.insert_or_assign(actor, std::move(cb));
  std::lock_guard<std::mutex> lock(_actor_maps_mutex);
  _actor_by_id.insert_or_assign(actor_id, actor);
  _id_by_actor.insert_or_assign(actor, actor_id);
  _ego_actor_id = actor_id;
}

void ROS2::DrainActorCallbacksForTest() { DrainExtensionPendingCommands(); }

void ROS2::InjectSubscriberForTest(
    void *actor, std::shared_ptr<BaseSubscriber> subscriber) {
  _subscribers.insert({actor, std::move(subscriber)});
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
      // Skip auto-naming resolution when the sensor has a verbatim
      // ros_topic_name override: BuildBaseTopicName never consults ros_name in
      // that case, so resolving the "dvs__" placeholder would be pointless
      // mutation. has_override also tells the point-cloud side
      // (CarlaDVSPointCloudPublisher, via the composite) to skip the
      // "/point_cloud" suffix append (see CarlaPointCloudPublisher::Init) so
      // the override topic is emitted exactly as configured — mirrors the
      // ESensors::RayCastLidar branch below for the rest of the point-cloud
      // publisher family.
      const auto reg_it = _registrations.find(actor);
      const bool has_override =
          reg_it != _registrations.end() && !reg_it->second.ros_topic_name.empty();
      if (!has_override) {
        resolve("dvs");
      }
      publisher = std::make_shared<CarlaDVSCameraPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor), has_override);
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
      // See ESensors::DVSCamera above / ESensors::RayCastLidar below: same
      // has_override skip-resolve / skip-suffix rationale, mirrored here for
      // the "radar__" placeholder.
      const auto reg_it = _registrations.find(actor);
      const bool has_override =
          reg_it != _registrations.end() && !reg_it->second.ros_topic_name.empty();
      if (!has_override) {
        resolve("radar");
      }
      publisher = std::make_shared<CarlaRadarPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor), has_override);
      break;
    }
    case ESensors::RayCastSemanticLidar: {
      // See ESensors::DVSCamera above / ESensors::RayCastLidar below: same
      // has_override skip-resolve / skip-suffix rationale, mirrored here for
      // the "ray_cast_semantic__" placeholder.
      const auto reg_it = _registrations.find(actor);
      const bool has_override =
          reg_it != _registrations.end() && !reg_it->second.ros_topic_name.empty();
      if (!has_override) {
        resolve("ray_cast_semantic");
      }
      publisher = std::make_shared<CarlaSemanticLidarPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor), has_override);
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
      // Opt-in 10-float PointXYZIRCAEDT layout: baked into the publisher at
      // creation time so its field table / point_step are fixed for the
      // publisher's lifetime (a mid-stream layout switch would desync
      // subscribers). Defaults to false for an unregistered/plain lidar.
      const bool extended = reg_it != _registrations.end() && reg_it->second.extended_lidar;
      publisher = std::make_shared<CarlaLidarPublisher>(
          BuildBaseTopicName(actor), LookupFrameId(actor), has_override, qos, extended);
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

std::shared_ptr<BasePublisher> ROS2::GetOrCreateRadarSensorForTest(
    carla::streaming::detail::stream_id_type id, void *actor) {
  return GetOrCreateSensor(ESensors::Radar, id, actor);
}

std::shared_ptr<BasePublisher> ROS2::GetOrCreateSemanticLidarSensorForTest(
    carla::streaming::detail::stream_id_type id, void *actor) {
  return GetOrCreateSensor(ESensors::RayCastSemanticLidar, id, actor);
}

std::shared_ptr<CarlaTransformPublisher> ROS2::GetOrCreateTransformPublisher(void *actor) {
  if (!_publish_tf_global) {
    return nullptr;                       // global suppression: Autoware owns TF
  }
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
    if (publisher->IsExtended()) {
      // Extended (canonical 32-byte PointXYZIRCAEDT) path: assemble a
      // LidarPointEx[] by zipping the flat x/y/z/intensity from _points with the
      // parallel _points_extra companion (channel/azimuth/elevation/distance,
      // filled 1:1 by the sensor). intensity is quantized from CARLA's float
      // attenuation factor to the canonical UINT8 (QuantizeIntensity);
      // return_type comes from the companion. time_stamp is the per-point
      // nanosecond offset from the message header stamp — CARLA delivers a whole
      // scan at one simulation tick, so the offset is 0 for every point and the
      // absolute time lives in header.stamp (set by WritePointCloud). Points are
      // left in the CARLA/UE frame here; the publisher applies the ROS Y/azimuth
      // flip in ComputePointCloud, exactly as the legacy 16-byte path flips y.
      //
      // Guard on the companion length: the sensor's extended flag and the
      // publisher's are both derived from ros2_extended_lidar so they agree in
      // practice, but if the companion is short (e.g. a first frame before the
      // sensor observed the attribute) publish only the points we have full
      // data for rather than reading past _points_extra.
      const auto ex_width =
          static_cast<std::uint32_t>(std::min<std::size_t>(width, data._points_extra.size()));
      std::vector<LidarPointEx> packed(ex_width);
      for (std::uint32_t i = 0; i < ex_width; ++i) {
        const auto &extra = data._points_extra[i];
        LidarPointEx &p = packed[i];
        p.x = data._points[i * 4u + 0u];
        p.y = data._points[i * 4u + 1u];
        p.z = data._points[i * 4u + 2u];
        p.intensity = QuantizeIntensity(data._points[i * 4u + 3u]);
        p.return_type = extra.return_type;
        p.channel = extra.channel;
        p.azimuth = extra.azimuth;
        p.elevation = extra.elevation;
        p.distance = extra.distance;
        p.time_stamp = 0u;
      }
      publisher->WritePointCloud(
          _seconds, _nanoseconds, 1u, ex_width,
          reinterpret_cast<const std::uint8_t *>(packed.data()));
    } else {
      publisher->WritePointCloud(
          _seconds, _nanoseconds, 1u, width,
          reinterpret_cast<const std::uint8_t *>(data._points.data()));
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
    const carla::rpc::VehicleControl &control,
    float front_wheel_steer_angle_deg) {
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

  // Out-of-tree extension tap: fan the same per-frame ego state out to any
  // registered VEHICLE_STATUS observer. Skipped entirely when no extension has
  // registered, so a non-extension run pays only a vector empty-check.
  // _ext_rosname_scratch gives ros_name stable char* storage for the duration
  // of the synchronous dispatch (see the observer contract in
  // CarlaRos2Extension.h).
  if (!_ext_observers.empty()) {
    _ext_rosname_scratch = LookupRosName(actor);

    // CARLA left-handed CENTIMETRES + quaternion (per CarlaRos2Transform's ABI
    // contract in CarlaRos2Extension.h): keep location and orientation in the same
    // raw CARLA frame so the extension applies its own Autoware conversion
    // consistently. vehicle_transform.location is carla::geom METRES (the odometry
    // block above writes it straight into a ROS metre pose), so it is scaled to
    // centimetres inside MakeExtensionTransformMetresDeg -- without that scale the
    // extension's /100 left the synthesised GNSS pose ~350 m off the ego. The
    // quaternion is the CARLA-frame Euler->quaternion of vehicle_transform's
    // rotation (no handedness flip, unlike the ROS odometry quaternion above).
    const CarlaRos2Transform transform = MakeExtensionTransformMetresDeg(
        vehicle_transform.location.x, vehicle_transform.location.y, vehicle_transform.location.z,
        vehicle_transform.rotation.roll, vehicle_transform.rotation.pitch,
        vehicle_transform.rotation.yaw);

    // Steering: front-wheel road-wheel angle from the UE side (CARLA convention
    // is right-turn-positive on the FL wheel), converted deg->rad and NEGATED so
    // the view carries the Autoware convention (left-positive) directly — this
    // matches how the PythonAPI/ros-bridge negates CARLA's FL-wheel angle.
    // CAVEAT (this build): ACarlaWheeledVehicle::GetWheelSteerAngle is currently
    // engine-stubbed to return 0.0 on UE5/Chaos (the real readback is #if 0'd,
    // "@CARLAUE5 ToDo"), so front_wheel_steer_angle_deg is 0 until that stub is
    // implemented. The seam and sign are wired correctly for when it is; this is
    // a documented engine limitation, not a silent host-side zero.
    const double steering_tire_angle_rad =
        -static_cast<double>(front_wheel_steer_angle_deg) * carla::geom::Math::Pi<double>() / 180.0;

    DispatchVehicleStatusView(
        LookupActorId(actor),  // 0 until RegisterVehicle records it
        _ext_rosname_scratch.c_str(),
        transform,
        body_velocity.x,           // signed longitudinal body-frame speed (m/s)
        body_velocity.y,           // lateral body-frame velocity (m/s)
        ros_angular_velocity.z,    // yaw rate (rad/s)
        steering_tire_angle_rad,
        control.gear,
        static_cast<double>(_seconds) + _nanoseconds * 1e-9);
  }
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
  // Extension seam: drop any still-registered observers and the actor-id
  // bookkeeping. TeardownExtensionEndpoints() already clears the observers
  // before dlclose on the normal path; this is the belt-and-braces reset for a
  // Shutdown that is not preceded by a teardown.
  _ext_observers.clear();
  _ext_rosname_scratch.clear();
  {
    std::lock_guard<std::mutex> lock(_ext_pending_cmds_mutex);
    _ext_pending_cmds.clear();
  }
  {
    std::lock_guard<std::mutex> lock(_actor_maps_mutex);
    _actor_by_id.clear();
    _id_by_actor.clear();
    _ego_actor_id = 0;
  }
  _enabled = false;
#if defined(WITH_ROS2_DEMO)
  _basic_publisher.reset();
  _basic_subscriber.reset();
#endif
}

}  // namespace ros2
}  // namespace carla
