// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <util/ue-header-guard-begin.h>
#include "CoreMinimal.h"
#include <util/ue-header-guard-end.h>

// Pure C ABI, DDS-free by contract (see the header's own comment) — safe to
// include directly alongside UE headers, no macro-isolation wrapper needed.
#include "carla/ros2/extension/CarlaRos2Extension.h"

// RAII dlopen wrapper for an out-of-tree ROS 2 extension .so (--ros2-extension=).
// Load() runs the two-sided api_version handshake documented in
// CarlaRos2Extension.h and aborts the load (logging at Error) on ANY failure:
// dlopen failure, missing carla_ros2_extension_init symbol, a nonzero return
// from init, or an api_version mismatch after init returns 0. On abort, no
// partial state is kept (the .so is dlclose'd if it was opened) and the
// caller's ROS2 instance keeps running without the extension.
//
// Lifetime ordering (enforced by the caller, FCarlaEngine):
//   dlopen  happens AFTER ROS2->Enable() succeeds (the participant must exist
//           before the extension's host vtable is handed out).
//   dlclose happens BEFORE ROS2->Shutdown() (Unload() runs ext.on_shutdown
//           then dlclose while the ROS2 participant/publishers are still
//           alive, so the extension can release DDS-facing resources it
//           created through the host vtable before the participant itself is
//           torn down).
class CarlaRos2ExtensionLoader
{
public:

  // Load the .so, run the handshake, keep the extension vtable. Returns false
  // (and leaves nothing loaded) on any failure; the caller keeps ROS2 running.
  bool Load(const FString &Path, const CarlaRos2Host &Host);

  void Tick(double SimTimeSeconds);

  void Unload();

  ~CarlaRos2ExtensionLoader()
  {
    Unload();
  }

private:

  void *_handle = nullptr;

  CarlaRos2Extension _ext = {};

  bool _loaded = false;
};
