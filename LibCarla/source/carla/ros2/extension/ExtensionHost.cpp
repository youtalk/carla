// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//
// Host-side extension seam, compiled into carla-server (WITH_ROS2, NO DDS
// vendor macros). Every function here is a thin trampoline — the observer/actor
// slots recover the ROS2 singleton from host_ctx and forward to a ROS2 member;
// the blob pub/sub slots forward to the CycloneDDS-linked BlobCreate*/BlobPublish
// functions declared in the DDS-free ExtensionBlobEndpoints.h (defined in the
// carla-ros2-native TU, resolved at link time). This TU therefore stays DDS-free
// by contract (see ExtensionHost.h). The apply_ackermann_control slot lives here
// too: it forwards to ROS2::ApplyExtensionAckermann (a ROS2 member, no DDS
// entity), so it belongs on the DDS-free side with the other host_ctx slots.

#include "carla/ros2/extension/ExtensionHost.h"
#include "carla/ros2/extension/ExtensionBlobEndpoints.h"
#include "carla/ros2/ROS2.h"

namespace carla {
namespace ros2 {

// host_ctx is always the ROS2 singleton pointer MakeExtensionHost() stored, so
// these casts are safe: the extension only ever passes back the host_ctx it was
// handed.
static void host_register_sensor_observer(
    void *ctx, int kind, CarlaRos2SensorObserver cb, void *user) {
  static_cast<ROS2 *>(ctx)->RegisterExtensionObserver(kind, cb, user);
}

static uint32_t host_get_ego_actor_id(void *ctx) {
  return static_cast<ROS2 *>(ctx)->GetEgoActorIdForExtension();
}

static const char *host_get_actor_ros_name(void *ctx, uint32_t actor_id) {
  return static_cast<ROS2 *>(ctx)->GetActorRosNameForExtension(actor_id);
}

// Actuation sink: unpack the ABI pod into the internal AckermannControl and hand
// it to ROS2::ApplyExtensionAckermann, which STAGES it for the game-thread
// SetFrame drain (see ROS2.h). DDS-free: this routes through the ROS2 singleton,
// not a DDS entity, so it stays in this TU with the other host_ctx slots.
static void host_apply_ackermann_control(
    void *ctx, uint32_t actor_id, const CarlaRos2AckermannPod *pod) {
  if (pod == nullptr) {
    return;  // a null pod would dereference; the extension must supply one
  }
  AckermannControl cmd;
  cmd.steer = pod->steer;
  cmd.steer_speed = pod->steer_speed;
  cmd.speed = pod->speed;
  cmd.acceleration = pod->acceleration;
  cmd.jerk = pod->jerk;
  static_cast<ROS2 *>(ctx)->ApplyExtensionAckermann(actor_id, cmd);
}

// Blob pub/sub slots. Unlike the observer/actor slots these need no host_ctx:
// the writer/reader registry is a process-global in the DDS-linked TU, keyed by
// opaque handle, so the ROS2 singleton is irrelevant to endpoint identity.
static CarlaRos2PubHandle host_create_publisher(
    void * /*ctx*/, const char *topic, const char *type_name,
    const char *type_hash, const CarlaRos2Qos *qos) {
  return BlobCreatePublisher(topic, type_name, type_hash, qos);
}

static int host_publish(void * /*ctx*/, CarlaRos2PubHandle h,
                        const uint8_t *cdr, size_t len) {
  return BlobPublish(h, cdr, len);
}

static CarlaRos2SubHandle host_create_subscriber(
    void * /*ctx*/, const char *topic, const char *type_name,
    const char *type_hash, const CarlaRos2Qos *qos,
    CarlaRos2SubCallback cb, void *user) {
  return BlobCreateSubscriber(topic, type_name, type_hash, qos, cb, user);
}

CarlaRos2Host MakeExtensionHost() {
  CarlaRos2Host h = {};
  h.api_version = CARLA_ROS2_EXTENSION_API_VERSION;
  h.host_ctx = ROS2::GetInstance().get();
  h.register_sensor_observer = &host_register_sensor_observer;
  h.get_ego_actor_id = &host_get_ego_actor_id;
  h.get_actor_ros_name = &host_get_actor_ros_name;
  h.create_publisher = &host_create_publisher;
  h.publish = &host_publish;
  h.create_subscriber = &host_create_subscriber;
  h.apply_ackermann_control = &host_apply_ackermann_control;
  return h;
}

void TeardownExtensionEndpoints() {
  // Reclaim host-owned, extension-registered state BEFORE the loader runs the
  // extension's on_shutdown + dlclose. Destroy the DDS readers/writers FIRST
  // (BlobTeardownAll, in the DDS-linked TU): a reader's data-available listener
  // holds function pointers into the .so (br->cb) and the ExtensionState
  // (br->user), so it must be silenced before either goes away. Then drop the
  // observer registry. The shared participant is a process-lifetime static and
  // is intentionally NOT torn down here, so this ordering satisfies the ABI's
  // "destroy endpoints before participant teardown" contract trivially.
  BlobTeardownAll();
  ROS2::GetInstance()->ClearExtensionObservers();
}

}  // namespace ros2
}  // namespace carla
