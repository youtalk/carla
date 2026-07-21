// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include "carla/Buffer.h"
#include "carla/BufferView.h"
#include "carla/geom/Transform.h"
#include "carla/ros2/ROS2CallbackData.h"
#include "carla/ros2/extension/CarlaRos2Extension.h"
#include "carla/ros2/middleware/Middleware.h"
#include "carla/ros2/middleware/MiddlewareConfig.h"
#include "carla/ros2/middleware/PublisherQos.h"
#include "carla/streaming/detail/Types.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// forward declarations
class AActor;
namespace carla {
  namespace geom {
    class GeoLocation;
    struct Vector3D;
  }
  namespace rpc {
    class VehicleControl;
    struct VehiclePhysicsControl;
  }
  namespace sensor {
    namespace data {
      struct DVSEvent;
      class LidarData;
      class SemanticLidarData;
      class RadarData;
    }
  }
}

namespace carla {
namespace ros2 {

class BasePublisher;
class BaseSubscriber;
class CarlaCameraPublisher;
class CarlaClockPublisher;
class CarlaMapPublisher;
class CarlaTransformPublisher;
class CarlaOdometryPublisher;
class CarlaEgoVehicleStatusPublisher;
class CarlaEgoVehicleInfoPublisher;
class BasicSubscriber;
class BasicPublisher;

class ROS2 {
public:
  // deleting copy constructor for singleton
  ROS2(const ROS2 &obj) = delete;
  static std::shared_ptr<ROS2> GetInstance() {
    if (!_instance)
      _instance = std::shared_ptr<ROS2>(new ROS2);
    return _instance;
  }

  // general
  /// Enable or disable ROS 2 publishing. When enabling, @a middleware selects the
  /// DDS middleware; it must have been compiled into the shared library.
  /// @a domain_id selects the ROS 2 domain id for the chosen middleware;
  /// kUnsetDomainId (the default) keeps each middleware's native default.
  /// @return true on success, false if the requested middleware is unavailable
  /// (ROS 2 is then left disabled).
  bool Enable(bool enable, Middleware middleware = Middleware::FastDDS,
      int domain_id = kUnsetDomainId);
  void Shutdown();
  bool IsEnabled() { return _enabled; }
  void SetFrame(uint64_t frame);
  void SetTimestamp(double timestamp);

  // World-global TF publishing switch (World::SetPublishTF RPC). Disable when
  // an external stack (e.g. Autoware) owns the localization TF tree: once
  // false, GetOrCreateTransformPublisher returns nullptr for every actor
  // regardless of the per-actor publish_tf flag set at RegisterSensor time.
  void SetPublishTF(bool enabled) { _publish_tf_global = enabled; }
  bool GetPublishTF() const { return _publish_tf_global; }

  // actor registration API: replaces the legacy AddActorRosName /
  // GetActorRosName / GetActorParentRosName surface that PR-2 stubbed and
  // PR-4 retired. The plugin calls RegisterSensor / RegisterVehicle when an
  // actor spawns and Unregister* when it destroys; ROS2 builds the topic
  // names, owns per-sensor publishers + TF, and routes subscriber
  // callbacks. The vehicle gets exactly one control subscriber: the Ackermann
  // subscriber when enable_ackermann_control is true, otherwise the direct
  // VehicleControl one. The two control topics are mutually exclusive so they
  // cannot contend frame to frame.
  // qos is only ever consumed by GetOrCreateSensor's lidar branch today (see
  // ActorDispatcher's ros2_qos_* attribute parsing, itself lidar-only via
  // MakeLidarDefinition) — every other sensor type stores it in the
  // registration but never reads it back. Defaults to SensorData()
  // (best_effort/volatile/depth1), matching the wire behavior every
  // CarlaPointCloudPublisher subclass had before per-sensor QoS support
  // existed, so a caller that omits qos (or a lidar spawned without the
  // ros2_qos_* attributes) reproduces the pre-QoS-support default rather than
  // silently upgrading to a subscriber-blocking Reliable writer.
  // extended_lidar opts a ray-cast / HSS lidar into the 10-float
  // PointXYZIRCAEDT layout (blueprint attribute ros2_extended_lidar), parsed by
  // ActorDispatcher::RegisterActor alongside ros_topic_name / qos. Like qos it
  // is only ever read back by GetOrCreateSensor's lidar branch (passed to the
  // CarlaLidarPublisher ctor); every other sensor stores it but never uses it.
  // Defaults to false so an omitted attribute reproduces the 16-byte XYZI wire
  // layout exactly.
  void RegisterSensor(
      void *actor, std::string ros_name, std::string frame_id, bool publish_tf,
      std::string ros_topic_name = "", PublisherQos qos = PublisherQos::SensorData(),
      bool extended_lidar = false);

  // Test-only accessor: BuildBaseTopicName itself stays private since it is an
  // internal composition helper, not part of the actor-registration API.
  std::string BuildBaseTopicNameForTest(void *actor) const { return BuildBaseTopicName(actor); }

  // Test-only accessor: the per-sensor QoS is otherwise only observable via
  // the wire (DDS discovery), so unit tests read the registration directly.
  PublisherQos LookupSensorQosForTest(void *actor) const {
    auto it = _registrations.find(actor);
    return it == _registrations.end() ? PublisherQos() : it->second.qos;
  }

  // Test-only accessor: the extended-lidar flag is otherwise only observable via
  // the emitted point_step on the wire, so unit tests read the registration.
  bool LookupSensorExtendedForTest(void *actor) const {
    auto it = _registrations.find(actor);
    return it != _registrations.end() && it->second.extended_lidar;
  }

  // Test-only accessor: exercises the private GetOrCreateTransformPublisher
  // lazy-create/gate logic (including the _publish_tf_global early-return)
  // without needing a live DDS publisher round-trip.
  std::shared_ptr<CarlaTransformPublisher> GetOrCreateTransformPublisherForTest(void *actor) {
    return GetOrCreateTransformPublisher(actor);
  }

  // Test-only accessors: exercise the private GetOrCreateSensor dispatch for
  // the Radar / RayCastSemanticLidar branches (including the has_topic_override
  // plumbing) without needing a full ProcessDataFromRadar/SemanticLidar call
  // with real sensor payloads. The ESensors enum they bake in is TU-local to
  // ROS2.cpp, so these thin wrappers (defined there) are the only way to reach
  // a specific branch from outside; see CarlaPointCloudPublisher::HasTopicOverride
  // for the assertion these enable.
  std::shared_ptr<BasePublisher> GetOrCreateRadarSensorForTest(
      carla::streaming::detail::stream_id_type id, void *actor);
  std::shared_ptr<BasePublisher> GetOrCreateSemanticLidarSensorForTest(
      carla::streaming::detail::stream_id_type id, void *actor);

  void UnregisterSensor(void *actor);
  // actor_id is the CARLA actor id (FCarlaActorView::GetActorId()); RegisterVehicle
  // records the id<->actor* bookkeeping and marks this actor as the ego so the
  // extension seam's LookupActorId / GetEgoActorIdForExtension / ApplyExtension
  // Ackermann resolve it (all return the zero/empty sentinel until a vehicle
  // registers).
  void RegisterVehicle(
      void *actor, uint32_t actor_id, std::string ros_name, std::string frame_id,
      ActorCallback callback, bool enable_ackermann_control = false);
  void UnregisterVehicle(void *actor);

  // True when RegisterVehicle created the per-vehicle data publishers for
  // this actor and UnregisterVehicle has not destroyed them yet.
  bool IsVehicleRegistered(void *actor) const;

  // ---------------------------------------------------------------------------
  // Out-of-tree ROS 2 extension seam (host side). MakeExtensionHost() in
  // ExtensionHost.cpp routes the CarlaRos2Host vtable slots into these members.
  // RegisterExtensionObserver appends a (kind, callback, user) observer that is
  // invoked SYNCHRONOUSLY on the dispatch thread that produces a sample; the v1
  // extension observes only CARLA_ROS2_SENSOR_VEHICLE_STATUS (the per-frame ego
  // status stream tapped inside ProcessDataFromVehicle). The buffers handed to
  // an observer are valid ONLY for the duration of the call (see the observer
  // contract in CarlaRos2Extension.h).
  void RegisterExtensionObserver(int kind, CarlaRos2SensorObserver cb, void *user);
  // Drops every registered observer. Called by TeardownExtensionEndpoints()
  // before the extension's on_shutdown/dlclose so a stale function pointer into
  // an unloaded .so can never be dispatched into.
  void ClearExtensionObservers();
  // Returns the CARLA actor id of the single RegisterVehicle (hero) actor, or 0
  // if none is registered. Backed by _ego_actor_id (populated by RegisterVehicle
  // in Task 14).
  uint32_t GetEgoActorIdForExtension() const;
  // Resolves an actor id to its registered ros_name. The returned pointer is
  // owned by ROS2 and valid until the next call on the same thread.
  const char *GetActorRosNameForExtension(uint32_t actor_id) const;
  // Test-only driver for the VEHICLE_STATUS dispatch path: builds a POD view
  // from the given fields and fans it out to the registered observers exactly
  // like the ProcessDataFromVehicle tap does, without needing a live vehicle.
  void DispatchVehicleStatusObserversForTest(
      uint32_t actor_id, const char *ros_name, double velocity_mps,
      double steer_rad, int32_t gear, double sim_t);

  // Actuation counterpart of the observer seam: the CarlaRos2Host vtable's
  // apply_ackermann_control slot (ExtensionHost.cpp) routes here. It resolves
  // `actor_id` to the registered actor and STAGES the command in a small queue
  // drained inside SetFrame — it never applies inline. That is deliberate: the
  // extension may call this from its subscriber-listener thread or from on_tick,
  // whereas the ActorROS2Handler visit it ultimately feeds (ApplyVehicleAckermann
  // Control) must run on the game thread, exactly like the native control
  // subscriber whose callback is polled from SetFrame. Staging + the SetFrame
  // drain is the same structure CarlaEgoVehicleControlSubscriber uses, so the
  // extension path and the native path share the single _actor_callbacks apply
  // point and cannot introduce a second, off-thread actuation mechanism. An
  // unknown actor_id is dropped (loud in debug). The two paths stay mutually
  // exclusive on the wire (a vehicle subscribes to at most one control topic);
  // an extension driving actuation simply keeps that native subscriber idle.
  void ApplyExtensionAckermann(uint32_t actor_id, const AckermannControl &cmd);
  // Test-only: register an actor's control callback plus the id bookkeeping
  // (_actor_by_id / _id_by_actor / _ego_actor_id) that RegisterVehicle records
  // in the live path, so ApplyExtensionAckermann and the VEHICLE_STATUS tap can
  // resolve the actor without a live spawn.
  void RegisterVehicleCallbackForTest(void *actor, uint32_t actor_id, ActorCallback cb);
  // Test-only: run the SetFrame drain of the extension pending-command queue in
  // isolation (the live drain is one line inside SetFrame).
  void DrainActorCallbacksForTest();
  // Test-only: inject a subscriber into the per-actor subscriber map so a test
  // can drive the real SetFrame two-phase sequence (native subscriber loop, then
  // extension drain) with a stand-in native source and no live DDS message.
  void InjectSubscriberForTest(void *actor, std::shared_ptr<BaseSubscriber> subscriber);

  // Topic-hierarchy seam used by the plugin's attach_actor path: tells ROS2
  // that `actor` should publish under `parent`'s ros_name prefix. Walking
  // the parent chain is the publisher-side concern.
  void AddActorParentRosName(void *actor, void *parent);

  // Demo subscriber callbacks (only compiled when WITH_ROS2_DEMO).
  void RemoveBasicSubscriberCallback(void *actor);
  void AddBasicSubscriberCallback(
      void *actor, std::string ros_name, ActorMessageCallback callback);

  // enabling streams to publish
  void EnableStream(carla::streaming::detail::stream_id_type id) {
    _publish_stream.insert(id);
  }
  bool IsStreamEnabled(carla::streaming::detail::stream_id_type id) {
    return _publish_stream.count(id) > 0;
  }
  void ResetStreams() { _publish_stream.clear(); }

  // receiving data to publish
  void ProcessDataFromCamera(
      uint64_t sensor_type,
      carla::streaming::detail::stream_id_type stream_id,
      const carla::geom::Transform sensor_transform,
      int W, int H, float Fov,
      const carla::SharedBufferView buffer,
      void *actor = nullptr);
  void ProcessDataFromGNSS(
      uint64_t sensor_type,
      carla::streaming::detail::stream_id_type stream_id,
      const carla::geom::Transform sensor_transform,
      const carla::geom::GeoLocation &data,
      void *actor = nullptr);
  void ProcessDataFromIMU(
      uint64_t sensor_type,
      carla::streaming::detail::stream_id_type stream_id,
      const carla::geom::Transform sensor_transform,
      carla::geom::Vector3D accelerometer,
      carla::geom::Vector3D gyroscope,
      float compass,
      void *actor = nullptr);
  void ProcessDataFromDVS(
      uint64_t sensor_type,
      carla::streaming::detail::stream_id_type stream_id,
      const carla::geom::Transform sensor_transform,
      const carla::SharedBufferView buffer,
      int W, int H, float Fov,
      void *actor = nullptr);
  void ProcessDataFromLidar(
      uint64_t sensor_type,
      carla::streaming::detail::stream_id_type stream_id,
      const carla::geom::Transform sensor_transform,
      carla::sensor::data::LidarData &data,
      void *actor = nullptr);
  void ProcessDataFromSemanticLidar(
      uint64_t sensor_type,
      carla::streaming::detail::stream_id_type stream_id,
      const carla::geom::Transform sensor_transform,
      carla::sensor::data::SemanticLidarData &data,
      void *actor = nullptr);
  void ProcessDataFromRadar(
      uint64_t sensor_type,
      carla::streaming::detail::stream_id_type stream_id,
      const carla::geom::Transform sensor_transform,
      const carla::sensor::data::RadarData &data,
      void *actor = nullptr);
  void ProcessDataFromObstacleDetection(
      uint64_t sensor_type,
      carla::streaming::detail::stream_id_type stream_id,
      const carla::geom::Transform sensor_transform,
      AActor *first_actor,
      AActor *second_actor,
      float distance,
      void *actor = nullptr);
  void ProcessDataFromCollisionSensor(
      uint64_t sensor_type,
      carla::streaming::detail::stream_id_type stream_id,
      const carla::geom::Transform sensor_transform,
      uint32_t other_actor,
      carla::geom::Vector3D impulse,
      void *actor);
  // Publishes the OpenDRIVE description of the current map as a latched
  // topic. Called once per episode; re-publishing refreshes the latched
  // sample after a map change.
  void ProcessDataFromMap(const std::string &open_drive);
  // Publishes odometry and vehicle status for a registered vehicle. Called
  // once per frame. front_wheel_steer_angle_deg is the CARLA front-wheel
  // road-wheel angle in degrees (from ACarlaWheeledVehicle::GetWheelSteerAngle
  // on the FL wheel); it feeds ONLY the out-of-tree extension VEHICLE_STATUS
  // tap (converted to Autoware-convention radians there), never the odometry /
  // status publishers, so it defaults to 0 for callers that do not supply it.
  void ProcessDataFromVehicle(
      void *actor,
      const carla::geom::Transform vehicle_transform,
      carla::geom::Vector3D velocity,
      carla::geom::Vector3D angular_velocity,
      float delta_seconds,
      const carla::rpc::VehicleControl &control,
      float front_wheel_steer_angle_deg = 0.0f);
  // Publishes the latched static description of a registered vehicle.
  // Called once at registration.
  void ProcessVehicleInfo(
      void *actor,
      uint32_t id,
      const std::string &type_id,
      const std::string &role_name,
      const carla::geom::Transform vehicle_transform,
      const carla::rpc::VehiclePhysicsControl &physics_control);

private:
  struct ActorRegistration {
    std::string ros_name;
    std::string frame_id;
    std::string ros_topic_name;   // non-empty => verbatim topic, no composition
    bool publish_tf{true};
    // Defaults to SensorData() (best_effort/volatile/depth1) so this struct
    // default matches RegisterSensor's own default parameter exactly.
    // RegisterVehicle's designated-init (ActorRegistration{...}) never sets
    // .qos explicitly, so before this change it silently fell back to the
    // plain PublisherQos{} struct default (Reliable) instead — a divergence
    // between the vehicle and sensor registration paths with no functional
    // consequence today (qos is lidar-only, see below) but a footgun for any
    // future reader who fell back to .qos on a vehicle registration. Lidar
    // sensors get their QoS parsed from the ros2_qos_* blueprint attributes
    // (see ActorDispatcher::RegisterActor); every other sensor/actor type
    // stores this default but never reads it back.
    PublisherQos qos{PublisherQos::SensorData()};
    // Opt-in 10-float PointXYZIRCAEDT layout (ros2_extended_lidar). Same
    // lidar-only lifecycle as qos: set by RegisterSensor, consumed only by
    // GetOrCreateSensor's lidar branch.
    bool extended_lidar{false};
  };

  // Resolves an actor's `rt/carla/[parent/]ros_name` base topic by walking the
  // parent chain. Returns empty if the actor is not registered.
  std::string BuildBaseTopicName(void *actor) const;
  std::string LookupRosName(void *actor) const;
  std::string LookupFrameId(void *actor) const;
  std::string BuildParentChain(void *actor) const;

  // Extension-seam actor lookups (NEW in Task 12 — only LookupRosName(void*)
  // pre-existed). Backed by the _id_by_actor / _actor_by_id maps that
  // RegisterVehicle populates in Task 14; both return the empty/zero sentinel
  // until then.
  uint32_t LookupActorId(void *actor) const;
  std::string LookupRosNameById(uint32_t actor_id) const;

  // Single drain point for the extension-staged control commands, shared by the
  // live SetFrame drain and DrainActorCallbacksForTest so the two can never
  // drift. Invokes each staged command's _actor_callbacks entry (the same visit
  // the native control subscriber drives) and clears the queue. Runs on the
  // caller's thread — SetFrame calls it on the game thread.
  void DrainExtensionPendingCommands();

  // Single fill-and-fan-out point for the VEHICLE_STATUS stream, shared by the
  // live ProcessDataFromVehicle tap and DispatchVehicleStatusObserversForTest so
  // the POD layout the two produce can never drift. Runs the synchronous
  // dispatch loop over _ext_observers.
  void DispatchVehicleStatusView(
      uint32_t actor_id, const char *ros_name, const CarlaRos2Transform &transform,
      double velocity_mps, double lateral_velocity_mps, double yaw_rate_rps,
      double steering_tire_angle_rad, int32_t gear, double sim_time_s);

  // Lazy-creates the per-sensor publisher matching `type` (an ESensors enum
  // declared in ROS2.cpp). Returns the BasePublisher pointer; the caller
  // dynamic_pointer_casts to the concrete subtype for typed Write() calls.
  std::shared_ptr<BasePublisher> GetOrCreateSensor(
      int type, carla::streaming::detail::stream_id_type id, void *actor);

  // Lazy-creates the per-sensor transform publisher, gated on the actor's
  // publish_tf flag (set at RegisterSensor time). Returns nullptr if the
  // sensor opted out.
  std::shared_ptr<CarlaTransformPublisher> GetOrCreateTransformPublisher(void *actor);

  // Camera-side counterpart of GetOrCreateSensor for publishers that inherit
  // CarlaCameraPublisher (the RGB / Depth / SS / IS / Normals / OpticalFlow
  // unified base). DVS uses its own composite via GetOrCreateSensor.
  template <typename CameraT>
  std::shared_ptr<CarlaCameraPublisher> GetOrCreateCameraSensor(
      carla::streaming::detail::stream_id_type id,
      void *actor,
      const std::string &default_prefix);

  // Resolves a `prefix__` placeholder by appending the stream id, persisting
  // the resolved name in `_registrations` so subsequent lookups see it.
  void ResolveAutoStreamSuffix(
      void *actor, const std::string &prefix, carla::streaming::detail::stream_id_type id);

  // singleton
  ROS2() = default;

  static std::shared_ptr<ROS2> _instance;

  bool _enabled{false};
  uint64_t _frame{0};
  int32_t _seconds{0};
  uint32_t _nanoseconds{0};
  bool _publish_tf_global{true};

  std::unordered_map<void *, ActorRegistration> _registrations;
  std::unordered_map<void *, std::vector<void *>> _actor_parents;
  std::shared_ptr<CarlaClockPublisher> _clock_publisher;
  std::shared_ptr<CarlaMapPublisher> _map_publisher;
  std::unordered_map<void *, std::shared_ptr<BasePublisher>> _publishers;
  std::unordered_map<void *, std::shared_ptr<CarlaCameraPublisher>> _camera_publishers;
  std::unordered_map<void *, std::shared_ptr<CarlaTransformPublisher>> _transforms;
  std::unordered_set<carla::streaming::detail::stream_id_type> _publish_stream;
  std::unordered_map<void *, ActorCallback> _actor_callbacks;
  std::unordered_multimap<void *, std::shared_ptr<BaseSubscriber>> _subscribers;
#if defined(WITH_ROS2_DEMO)
  std::shared_ptr<BasicSubscriber> _basic_subscriber;
  std::shared_ptr<BasicPublisher> _basic_publisher;
  std::unordered_map<void *, ActorMessageCallback> _actor_message_callbacks;
#endif

  // Per-vehicle data publishers, created at RegisterVehicle and destroyed at
  // UnregisterVehicle/Shutdown.
  struct VehiclePublishers {
    std::shared_ptr<CarlaOdometryPublisher> odometry;
    std::shared_ptr<CarlaEgoVehicleStatusPublisher> status;
    std::shared_ptr<CarlaEgoVehicleInfoPublisher> info;
  };
  std::unordered_map<void *, VehiclePublishers> _vehicle_publishers;

  // Out-of-tree extension seam state (host-owned). An observer is a plain POD
  // triple; _ext_observers is appended by RegisterExtensionObserver and cleared
  // by ClearExtensionObservers (teardown). _ext_rosname_scratch gives the
  // VEHICLE_STATUS tap stable char* storage for the sample's ros_name across
  // the synchronous dispatch.
  struct ExtObserver {
    int kind;
    CarlaRos2SensorObserver cb;
    void *user;
  };
  std::vector<ExtObserver> _ext_observers;
  std::string _ext_rosname_scratch;

  // Ego / actor-id bookkeeping shared with Task 14. The ego is the single
  // RegisterVehicle actor, addressed by the extension via its CARLA actor id.
  // RegisterVehicle records all three in Task 14; declared here so Tasks 12 and
  // 14 share one definition (Task 12 reads them via LookupActorId /
  // GetEgoActorIdForExtension, which return the zero/empty sentinel until then).
  //
  // Guarded by _actor_maps_mutex: the writers (RegisterVehicle / UnregisterVehicle
  // / Shutdown) run on the game thread, but the extension seam READS these from a
  // FOREIGN thread by design — ApplyExtensionAckermann, GetEgoActorIdForExtension
  // and LookupRosNameById are reached from the host vtable, which the extension may
  // call off the game thread. A concurrent unordered_map::find during a writer's
  // rehash is UB (unlike the benign scalar read of _ego_actor_id), so EVERY access
  // to the three members below takes the lock; readers copy the id/pointer out and
  // release it before doing any further work (never held across a callback or UE
  // code). mutable so the const readers can lock.
  mutable std::mutex _actor_maps_mutex;
  uint32_t _ego_actor_id{0};
  std::unordered_map<uint32_t, void *> _actor_by_id;  // id -> actor*
  std::unordered_map<void *, uint32_t> _id_by_actor;  // actor* -> id

  // Extension actuation staging. ApplyExtensionAckermann records the LATEST
  // command per actor here (last-wins, one slot per actor — mirrors the native
  // subscriber's single-message slot and bounds growth if the game thread stalls
  // between drains); SetFrame drains it on the game thread right after the native
  // subscriber callbacks, so extension-sourced Ackermann commands reach
  // ApplyVehicleAckermannControl through the exact same _actor_callbacks visit as
  // the native control subscriber, one frame later. Because the drain runs AFTER
  // the subscriber loop, an extension command wins over a same-frame native one
  // for the same actor (pinned by a unit test).
  //
  // Guarded by _ext_pending_cmds_mutex because the producer may be the
  // extension's DDS subscriber-listener thread while the consumer (the SetFrame
  // drain) is the game thread. The drain swaps the map out under the lock and
  // invokes the callbacks AFTER releasing it, so no UE actuation code ever runs
  // while the lock is held (mirrors the FrameToProcessMutex-guarded frame vector
  // in CarlaEngine::OnPreTick).
  std::mutex _ext_pending_cmds_mutex;
  std::unordered_map<void *, ROS2CallbackData> _ext_pending_cmds;
};

}  // namespace ros2
}  // namespace carla
