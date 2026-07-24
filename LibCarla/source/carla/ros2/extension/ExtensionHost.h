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
// vtable from a SEPARATE translation unit that DOES link DDS (built into
// carla-ros2-native, see ExtensionBlobEndpoints.cpp) — keeping them out of
// this TU preserves the invariant that carla-server never odr-uses a DDS
// entity.

#pragma once

#include "carla/ros2/extension/CarlaRos2Extension.h"

namespace carla {
namespace ros2 {

// Builds the CarlaRos2Host vtable handed to carla_ros2_extension_init at Load()
// time. host_ctx is the ROS2 singleton pointer; the observer/actor-query and
// apply_ackermann_control slots route back through it (apply_ackermann_control
// forwards to ROS2::ApplyExtensionAckermann, which stages the command for the
// game-thread SetFrame drain). The create_publisher / publish / create_subscriber
// slots forward to the CycloneDDS-linked blob endpoints (defined in
// ExtensionBlobEndpoints.cpp, built into carla-ros2-native).
CarlaRos2Host MakeExtensionHost();

// Reclaims host-owned state the extension registered through the vtable, called
// by the loader BEFORE the extension's on_shutdown and before dlclose (see the
// endpoint-lifetime note in CarlaRos2Extension.h). It destroys every
// extension-created DDS reader/writer (BlobTeardownAll, from the DDS-linked TU)
// AND clears the sensor-observer registry, dropping every extension-supplied
// function pointer so neither a late sensor dispatch nor a data-available
// listener can call into a soon-to-be-unloaded .so or a freed extension state.
void TeardownExtensionEndpoints();

}  // namespace ros2
}  // namespace carla
