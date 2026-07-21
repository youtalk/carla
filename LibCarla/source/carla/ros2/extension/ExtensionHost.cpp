// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//
// Host-side extension seam, compiled into carla-server (WITH_ROS2, NO DDS
// vendor macros). Every function here is a thin trampoline that recovers the
// ROS2 singleton from host_ctx and forwards to a ROS2 member — this TU is
// DDS-free by contract (see ExtensionHost.h). The DDS-backed vtable slots
// (create_publisher / publish / create_subscriber / apply_ackermann_control)
// are filled from a separate DDS-linked TU in Tasks 13-14; they stay null here.

#include "carla/ros2/extension/ExtensionHost.h"
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

CarlaRos2Host MakeExtensionHost() {
  CarlaRos2Host h = {};
  h.api_version = CARLA_ROS2_EXTENSION_API_VERSION;
  h.host_ctx = ROS2::GetInstance().get();
  h.register_sensor_observer = &host_register_sensor_observer;
  h.get_ego_actor_id = &host_get_ego_actor_id;
  h.get_actor_ros_name = &host_get_actor_ros_name;
  // create_publisher / publish / create_subscriber / apply_ackermann_control
  // are wired from the DDS-linked TU in Tasks 13-14; left null here.
  return h;
}

void TeardownExtensionEndpoints() {
  // Task 12: the only host-owned, extension-registered state is the observer
  // registry; clear it so a late dispatch cannot call a function pointer into a
  // soon-to-be-dlclose'd .so. DDS reader/writer reclamation layers on in Task
  // 13 from the DDS-linked TU (this TU must stay DDS-free).
  ROS2::GetInstance()->ClearExtensionObservers();
}

}  // namespace ros2
}  // namespace carla
