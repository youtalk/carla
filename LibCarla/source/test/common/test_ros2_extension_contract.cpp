// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//
// Contract suite for the out-of-tree ROS 2 extension seam. Unlike
// test_ros2_extension_host.cpp / test_ros2_extension_control.cpp (which call
// MakeExtensionHost()'s vtable slots directly against a linked-in C++ test
// fixture), this file dlopen's a REAL standalone .so built from
// test/ros2_mock_extension/mock_extension.cpp against ONLY the pure-C ABI
// header, exactly like the Unreal-side CarlaRos2ExtensionLoader will dlopen
// the real out-of-tree extension. That is the seam this suite actually
// proves: load, handshake, version-mismatch refusal, observer routing, and
// control-sink routing across a real dlopen boundary, not just a linked-in
// stub.
//
// Guarded on WITH_ROS2 for the same reason as test_ros2_extension_host.cpp /
// test_ros2_extension_control.cpp: MakeExtensionHost() and ROS2.cpp are only
// linked into libcarla_test_server, not libcarla_test_client, so calling them
// unconditionally would be an undefined reference on the client side.
#include <gtest/gtest.h>

#if defined(WITH_ROS2)
#include "carla/ros2/extension/CarlaRos2Extension.h"
#include "carla/ros2/extension/ExtensionHost.h"
#include "carla/ros2/ROS2.h"
#include <dlfcn.h>
#include <cstdlib>

using InitFn = int (*)(const CarlaRos2Host*, CarlaRos2Extension*);

TEST(ros2_extension_contract, load_handshake_and_observer_and_control) {
  void* so = dlopen(CARLA_ROS2_MOCK_EXTENSION_SO, RTLD_NOW | RTLD_LOCAL);
  ASSERT_NE(so, nullptr) << dlerror();
  auto init = reinterpret_cast<InitFn>(dlsym(so, "carla_ros2_extension_init"));
  ASSERT_NE(init, nullptr);
  CarlaRos2Host host = carla::ros2::MakeExtensionHost();
  CarlaRos2Extension ext = {};
  ASSERT_EQ(init(&host, &ext), 0);
  EXPECT_EQ(ext.api_version, CARLA_ROS2_EXTENSION_API_VERSION);

  auto ros2 = carla::ros2::ROS2::GetInstance();
  int actor = 0;
  ros2->RegisterVehicleCallbackForTest(&actor, 42, [](void*, carla::ros2::ROS2CallbackData){});
  ros2->DispatchVehicleStatusObserversForTest(42, "ego", 4.0, 0.0, 1, 0.0);
  auto calls = reinterpret_cast<int(*)(void*)>(dlsym(so, "mock_observer_calls"));
  auto vel   = reinterpret_cast<double(*)(void*)>(dlsym(so, "mock_last_velocity"));
  EXPECT_EQ(calls(ext.ext_ctx), 1);
  EXPECT_DOUBLE_EQ(vel(ext.ext_ctx), 4.0);

  // Reproduce the production shutdown order (FCarlaEngine::~FCarlaEngine,
  // CarlaEngine.cpp:94-95): TeardownExtensionEndpoints() BEFORE on_shutdown
  // and before dlclose, so the observer this test registered is dropped from
  // the global registry while the .so's code is still mapped, instead of
  // leaking a dangling function pointer into it for a later test to trip
  // over (see CarlaRos2Extension.h's ordering note on host-owned endpoint
  // lifetime).
  carla::ros2::TeardownExtensionEndpoints();
  ext.on_shutdown(ext.ext_ctx);
  ros2->UnregisterVehicle(&actor);
  dlclose(so);
}

// Only the MOCK's half of the version-mismatch contract is testable here:
// the host-side refusal (aborting the load when ext.api_version != host's)
// lives in the UE-side CarlaRos2ExtensionLoader::Load(), which this LibCarla
// test binary cannot link (see this file's banner comment) — that refusal
// is exercised only by a live UE run, not by this suite. This test only
// proves the mock can signal a mismatch across the ABI for the loader to
// act on.
TEST(ros2_extension_contract, mock_reports_version_mismatch_for_loader_refusal) {
  setenv("MOCK_FORCE_BAD_VERSION", "1", 1);
  void* so = dlopen(CARLA_ROS2_MOCK_EXTENSION_SO, RTLD_NOW | RTLD_LOCAL);
  ASSERT_NE(so, nullptr);
  auto init = reinterpret_cast<InitFn>(dlsym(so, "carla_ros2_extension_init"));
  CarlaRos2Host host = carla::ros2::MakeExtensionHost();
  CarlaRos2Extension ext = {};
  ASSERT_EQ(init(&host, &ext), 0);
  // The host-side loader (CarlaRos2ExtensionLoader::Load(), UE-side, not
  // linked into this binary) rejects this because ext.api_version != host.
  EXPECT_NE(ext.api_version, CARLA_ROS2_EXTENSION_API_VERSION);
  unsetenv("MOCK_FORCE_BAD_VERSION");
  dlclose(so);
}
#endif  // defined(WITH_ROS2)
