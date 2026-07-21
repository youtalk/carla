// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//
// A REAL out-of-tree ROS 2 extension .so, built for test_ros2_extension_
// contract.cpp. Two-build-split contract: this TU includes ONLY the pure-C
// ABI header (CarlaRos2Extension.h) — exactly like the real out-of-tree
// extension repo would — never carla-server or ROS2 headers, so it proves
// the seam compiles and links standalone against nothing but the frozen ABI.
// It is dlopen'd by the TEST (not linked in), so the contract suite exercises
// a genuine same-process dlopen boundary rather than a linked-in C++ stub.
#include "carla/ros2/extension/CarlaRos2Extension.h"
#include <cstdlib>

namespace {

struct MockCtx {
  const CarlaRos2Host* host;
  int    observer_calls;
  double last_velocity;
};

// Observer for CARLA_ROS2_SENSOR_VEHICLE_STATUS: record the call and echo an
// Ackermann command back into the host on every tick, proving both the
// observer-routing and control-sink-routing halves of the seam in one pass.
void mock_observer(void* user, const CarlaRos2SensorSample* s) {
  auto* c = static_cast<MockCtx*>(user);
  ++c->observer_calls;
  auto* v = static_cast<const CarlaRos2VehicleStatusView*>(s->data);
  c->last_velocity = v->velocity_mps;
  CarlaRos2AckermannPod pod{0.0f, 0.0f, (float)v->velocity_mps, 1.0f, 0.0f};
  c->host->apply_ackermann_control(c->host->host_ctx, s->actor_id, &pod);
}

void mock_on_tick(void*, double) {}

void mock_on_shutdown(void* ext) { std::free(ext); }

}  // namespace

// Test hooks so the contract test can read the mock's state after dlopen
// (there is no other way to reach ext_ctx's fields across the dlopen
// boundary — it is opaque to the host by contract).
extern "C" int mock_observer_calls(void* ext) {
  return static_cast<MockCtx*>(ext)->observer_calls;
}
extern "C" double mock_last_velocity(void* ext) {
  return static_cast<MockCtx*>(ext)->last_velocity;
}

// The single exported entry point (CarlaRos2Extension.h's contract). Forces a
// version mismatch when MOCK_FORCE_BAD_VERSION is set, so the contract suite
// can exercise the refusal path without a second built artifact.
extern "C" int carla_ros2_extension_init(const CarlaRos2Host* host, CarlaRos2Extension* out) {
  if (!host || host->api_version != CARLA_ROS2_EXTENSION_API_VERSION) return 1;
  if (std::getenv("MOCK_FORCE_BAD_VERSION")) {
    out->api_version = 999u;
    return 0;
  }
  auto* ctx = static_cast<MockCtx*>(std::calloc(1, sizeof(MockCtx)));
  ctx->host = host;
  host->register_sensor_observer(host->host_ctx, CARLA_ROS2_SENSOR_VEHICLE_STATUS,
                                 mock_observer, ctx);
  out->api_version = CARLA_ROS2_EXTENSION_API_VERSION;
  out->ext_ctx = ctx;
  out->on_tick = mock_on_tick;
  out->on_shutdown = mock_on_shutdown;
  return 0;
}
