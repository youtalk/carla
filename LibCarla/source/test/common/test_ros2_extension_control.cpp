// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include <gtest/gtest.h>

// Guarded on WITH_ROS2 for the same reason as test_ros2_extension_host.cpp:
// MakeExtensionHost() and ROS2.cpp (the real targets here) are only linked into
// libcarla_test_server, not libcarla_test_client, so calling them
// unconditionally would be an undefined reference on the client side.
#if defined(WITH_ROS2)
#include "carla/ros2/extension/CarlaRos2Extension.h"
#include "carla/ros2/extension/ExtensionHost.h"
#include "carla/ros2/ROS2.h"
#include "carla/ros2/subscribers/BaseSubscriber.h"

#include <memory>
#include <variant>
#include <vector>

namespace {

// Minimal in-process subscriber that delivers exactly one native VehicleControl
// the first time SetFrame polls it, standing in for the real DDS control
// subscriber without a live message. It lets the ordering test drive the REAL
// SetFrame two-phase sequence (native subscriber loop, then extension drain).
class OneShotControlSubscriber : public carla::ros2::BaseSubscriber {
public:
  explicit OneShotControlSubscriber(void *actor)
      : carla::ros2::BaseSubscriber(actor, std::string{}, std::string{}) {}

  void ProcessMessages(carla::ros2::ActorCallback callback) override {
    if (_delivered) return;
    _delivered = true;
    callback(GetActor(), carla::ros2::ROS2CallbackData{carla::ros2::VehicleControl{}});
  }

protected:
  carla::ros2::ROS2CallbackData GetMessage() override {
    return carla::ros2::ROS2CallbackData{carla::ros2::VehicleControl{}};
  }

private:
  bool _delivered = false;
};

}  // namespace

// The extension actuation seam: an Ackermann pod handed to the host vtable's
// apply_ackermann_control slot must reach the SAME per-actor ActorCallback the
// native control subscriber drives (no new apply mechanism), and it must be
// STAGED — not invoked inline on the calling thread — so the ActorROS2Handler
// visit runs on the game thread inside SetFrame's drain, exactly like the
// subscriber path. DrainActorCallbacksForTest() stands in for that SetFrame drain.
TEST(ros2_extension_control, ackermann_pod_routes_to_actor_callback) {
  auto ros2 = carla::ros2::ROS2::GetInstance();
  int actor = 0;
  carla::ros2::AckermannControl got = {};
  int calls = 0;
  ros2->RegisterVehicleCallbackForTest(&actor, /*actor_id=*/9,
    [&](void*, carla::ros2::ROS2CallbackData d) {
      if (auto* a = std::get_if<carla::ros2::AckermannControl>(&d)) { got = *a; ++calls; }
    });

  CarlaRos2Host h = carla::ros2::MakeExtensionHost();
  ASSERT_NE(h.apply_ackermann_control, nullptr);

  CarlaRos2AckermannPod pod{0.2f, 1.0f, 5.0f, 1.5f, 0.0f};
  h.apply_ackermann_control(h.host_ctx, 9, &pod);

  // Staging invariant: the command is queued, NOT applied on the calling thread.
  // Nothing reaches the actor callback until the game-thread drain runs.
  EXPECT_EQ(calls, 0);

  ros2->DrainActorCallbacksForTest();          // simulate SetFrame drain
  EXPECT_EQ(calls, 1);
  EXPECT_FLOAT_EQ(got.steer, 0.2f);
  EXPECT_FLOAT_EQ(got.steer_speed, 1.0f);
  EXPECT_FLOAT_EQ(got.speed, 5.0f);
  EXPECT_FLOAT_EQ(got.acceleration, 1.5f);
  EXPECT_FLOAT_EQ(got.jerk, 0.0f);

  ros2->UnregisterVehicle(&actor);
}

// An Ackermann command addressed to an actor id that was never registered is
// dropped, not staged: no callback fires on the next drain and the host does
// not crash. This exercises the ApplyExtensionAckermann unknown-id guard.
TEST(ros2_extension_control, unknown_actor_id_is_dropped) {
  auto ros2 = carla::ros2::ROS2::GetInstance();
  int actor = 0;
  int calls = 0;
  ros2->RegisterVehicleCallbackForTest(&actor, /*actor_id=*/9,
    [&](void*, carla::ros2::ROS2CallbackData) { ++calls; });

  CarlaRos2Host h = carla::ros2::MakeExtensionHost();
  CarlaRos2AckermannPod pod{0.2f, 1.0f, 5.0f, 1.5f, 0.0f};
  h.apply_ackermann_control(h.host_ctx, /*actor_id=*/1234, &pod);  // never registered
  ros2->DrainActorCallbacksForTest();
  EXPECT_EQ(calls, 0);

  ros2->UnregisterVehicle(&actor);
}

// Same-frame mutual exclusivity: when a native control delivery and an extension
// Ackermann both target the same actor within one frame, SetFrame runs the
// subscriber loop FIRST and the extension drain AFTER, so the extension command
// is applied last and wins. Pinning the order guards against a future reorder of
// the two phases inside SetFrame.
TEST(ros2_extension_control, extension_ackermann_wins_over_same_frame_native_control) {
  auto ros2 = carla::ros2::ROS2::GetInstance();
  int actor = 0;
  std::vector<int> order;  // 0 = native VehicleControl, 1 = extension AckermannControl
  ros2->RegisterVehicleCallbackForTest(&actor, /*actor_id=*/9,
    [&](void*, carla::ros2::ROS2CallbackData d) {
      order.push_back(std::holds_alternative<carla::ros2::AckermannControl>(d) ? 1 : 0);
    });
  ros2->InjectSubscriberForTest(&actor, std::make_shared<OneShotControlSubscriber>(&actor));

  CarlaRos2Host h = carla::ros2::MakeExtensionHost();
  CarlaRos2AckermannPod pod{0.2f, 1.0f, 5.0f, 1.5f, 0.0f};
  h.apply_ackermann_control(h.host_ctx, 9, &pod);  // stage the extension command

  ros2->SetFrame(1);  // native subscriber loop THEN extension drain, in that order

  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order.front(), 0);  // native VehicleControl delivered first
  EXPECT_EQ(order.back(), 1);   // extension Ackermann delivered last -> wins

  ros2->UnregisterVehicle(&actor);
}
#endif  // defined(WITH_ROS2)
