// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//
// DDS-FREE forward declarations for the CycloneDDS raw-CDR blob endpoints that
// back the extension host's create_publisher / publish / create_subscriber
// vtable slots. This header is included by BOTH:
//   * ExtensionHost.cpp (carla-server, NO DDS vendor macros) — which only calls
//     these functions to fill the vtable and to reclaim endpoints at teardown, and
//   * ExtensionBlobEndpoints.cpp (carla-ros2-native, CycloneDDS-linked) — which
//     defines them.
// It must therefore stay DDS-free: it depends only on the pure-C ABI header
// CarlaRos2Extension.h. Keeping the definitions in the DDS-linked TU (globbed
// into carla-ros2-native, NOT carla-server) preserves the split whereby
// carla-server never odr-uses a DDS entity (see CarlaRos2Extension.h's note
// on the header-inline-ctor trap this split avoids): carla-server references
// these symbols only through the declarations below, and the linker resolves
// them against libcarla-ros2-native.so.

#pragma once

#include "carla/ros2/extension/CarlaRos2Extension.h"

namespace carla {
namespace ros2 {

// Create a CycloneDDS writer for a raw-CDR "blob" topic. `topic` is an
// extension-supplied ROS topic name (e.g. "/ext/cmd"); it is mapped to its DDS
// wire name ("rt/ext/cmd") so ROS 2 nodes can see it. Returns a nonzero handle
// on success, or 0 on failure (and logs). When the process is not built/linked
// against CycloneDDS this is a loud no-op returning 0 (v1 supports CycloneDDS
// only).
CarlaRos2PubHandle BlobCreatePublisher(const char* topic, const char* type_name,
                                       const char* type_hash,
                                       const CarlaRos2Qos* qos);

// Publish pre-serialized CDR bytes (including the 4-byte encapsulation header)
// on a writer previously returned by BlobCreatePublisher. Returns 0 on success,
// -1 on an unknown handle or a write failure.
int BlobPublish(CarlaRos2PubHandle h, const uint8_t* cdr, size_t len);

// Create a CycloneDDS reader for a raw-CDR "blob" topic. The reader's
// data-available listener delivers each received sample's raw CDR bytes to `cb`
// (with `user`). Same topic-name mapping and CycloneDDS-only semantics as
// BlobCreatePublisher. Returns a nonzero handle on success, or 0 on failure.
CarlaRos2SubHandle BlobCreateSubscriber(const char* topic, const char* type_name,
                                        const char* type_hash,
                                        const CarlaRos2Qos* qos,
                                        CarlaRos2SubCallback cb, void* user);

// Delete EVERY extension-created reader/writer and clear the registries. The
// host owns endpoint lifetime (there is no per-endpoint destroy in the ABI by
// design); TeardownExtensionEndpoints() calls this from the core loader BEFORE
// the extension's on_shutdown + dlclose so no data-available listener can fire
// into a freed ExtensionState (br->user) or an unloaded .so (br->cb).
void BlobTeardownAll();

}  // namespace ros2
}  // namespace carla
