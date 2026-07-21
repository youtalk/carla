// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//
// Host-side (core) end of the out-of-tree ROS 2 extension seam. This header is
// DDS-FREE by contract: ExtensionHost.cpp is compiled into carla-server (which
// never links a DDS vendor), so it may only depend on the pure-C ABI header
// CarlaRos2Extension.h and ROS2.h. The CycloneDDS-backed blob endpoints
// (create_publisher / publish / create_subscriber) are wired into the host
// vtable from a SEPARATE translation unit that DOES link DDS (Ros2Native,
// Task 13) — keeping them out of this TU preserves the Phase A split whereby
// carla-server never odr-uses a DDS entity.

#pragma once

#include "carla/ros2/extension/CarlaRos2Extension.h"

namespace carla {
namespace ros2 {

// Builds the CarlaRos2Host vtable handed to carla_ros2_extension_init at Load()
// time. host_ctx is the ROS2 singleton pointer; every function pointer routes
// back through it. The observer/actor-query slots are filled here (Task 12);
// the create_publisher / publish / create_subscriber / apply_ackermann_control
// slots are filled by Tasks 13-14 and left null until then.
CarlaRos2Host MakeExtensionHost();

// Reclaims host-owned state the extension registered through the vtable, called
// by the loader BEFORE the extension's on_shutdown and before dlclose (see the
// endpoint-lifetime note in CarlaRos2Extension.h). In Task 12 the only such
// state is the sensor-observer registry: clearing it drops every
// extension-supplied function pointer so a late dispatch can never call into a
// soon-to-be-unloaded .so. The DDS reader/writer reclamation is layered on in
// Task 13 from the DDS-linked TU.
void TeardownExtensionEndpoints();

}  // namespace ros2
}  // namespace carla
