// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include <gtest/gtest.h>

// Guarded on WITH_ROS2 for the same reason as test_ros2_extension_host.cpp:
// MakeExtensionHost() lives in carla-server (linked into libcarla_test_server
// only), so referencing it unconditionally would be an undefined symbol on the
// client-side test binary. The frozen ABI layout itself is pinned separately by
// test_ros2_extension_abi.cpp.
#if defined(WITH_ROS2)
#include "carla/ros2/extension/CarlaRos2Extension.h"
#include "carla/ros2/extension/ExtensionHost.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

// --------------------------------------------------------------------------
// Seam-wiring tests (always compiled under WITH_ROS2).
//
// These never reference a DDS symbol directly: they exercise the blob endpoints
// only through the CarlaRos2Host vtable. libcarla_test_server is NOT compiled
// with CARLA_ROS2_MIDDLEWARE_CYCLONEDDS (that macro is private to
// carla-ros2-native), yet the vtable slots are function pointers resolved at
// link time against the CycloneDDS-linked .so — so calling h.publish() here
// runs the REAL BlobPublish out of libcarla-ros2-native.so. That is exactly the
// two-build seam (carla-server references BlobCreate*/BlobPublish/
// BlobTeardownAll through the DDS-free ExtensionBlobEndpoints.h forward
// declarations; the definitions live in the DDS-linked TU), so these
// tests prove the seam links end-to-end without needing a live participant.
// --------------------------------------------------------------------------

TEST(ros2_extension_blob, vtable_slots_are_wired) {
  CarlaRos2Host h = carla::ros2::MakeExtensionHost();
  // MakeExtensionHost() must wire all three of these slots to the DDS-linked
  // blob endpoints; a regression back to null would silently break the
  // publish/subscribe seam.
  EXPECT_NE(h.create_publisher, nullptr);
  EXPECT_NE(h.publish, nullptr);
  EXPECT_NE(h.create_subscriber, nullptr);
}

TEST(ros2_extension_blob, publish_with_unknown_handle_returns_error) {
  CarlaRos2Host h = carla::ros2::MakeExtensionHost();
  ASSERT_NE(h.publish, nullptr);
  const uint8_t msg[8] = {0x00u, 0x01u, 0x00u, 0x00u, 0xDEu, 0xADu, 0xBEu, 0xEFu};
  // Handle 0 is the documented "invalid" sentinel and 987654321 was never
  // handed out; both must miss the writer registry and return -1 (a clean map
  // lookup — no participant is created), not crash. This drives the real
  // BlobPublish failure path through the vtable seam.
  EXPECT_EQ(h.publish(h.host_ctx, 0u, msg, sizeof(msg)), -1);
  EXPECT_EQ(h.publish(h.host_ctx, 987654321u, msg, sizeof(msg)), -1);
}

// --------------------------------------------------------------------------
// In-process CDR round-trip (DDS-gated).
//
// Compiled in only when the translation unit sees CARLA_ROS2_MIDDLEWARE_CYCLONEDDS
// (i.e. a CycloneDDS-macro'd test build). The standard gate build does NOT
// define that macro, so this test elides here and contributes zero tests — the
// binding round-trip proof is a live pub/sub check against a real Autoware
// container, not an in-process DDS loopback in the unit gate. The body is kept
// faithful so it runs under a CycloneDDS build.
// --------------------------------------------------------------------------
#if defined(CARLA_ROS2_MIDDLEWARE_CYCLONEDDS)
namespace {
std::atomic<int> g_rx{0};
uint8_t g_buf[64];
size_t g_len = 0;
void sub_cb(void*, const uint8_t* cdr, size_t len) {
  g_len = len;
  std::memcpy(g_buf, cdr, len);
  ++g_rx;
}
}  // namespace

TEST(ros2_extension_blob, cdr_roundtrip_pub_to_sub) {
  CarlaRos2Host h = carla::ros2::MakeExtensionHost();
  ASSERT_NE(h.create_publisher, nullptr);
  ASSERT_NE(h.create_subscriber, nullptr);
  ASSERT_NE(h.publish, nullptr);
  CarlaRos2Qos qos{1u, 0u, 5u};  // best_effort, volatile, depth 5
  g_rx.store(0);
  auto sub = h.create_subscriber(h.host_ctx, "/test/blob",
                                 "std_msgs::msg::dds_::String_",
                                 "RIHS01_deadbeef", &qos, sub_cb, nullptr);
  auto pub = h.create_publisher(h.host_ctx, "/test/blob",
                                "std_msgs::msg::dds_::String_",
                                "RIHS01_deadbeef", &qos);
  ASSERT_NE(pub, 0u);
  ASSERT_NE(sub, 0u);
  // CDR: 4-byte encapsulation header + 4-byte payload.
  const uint8_t msg[8] = {0x00u, 0x01u, 0x00u, 0x00u,
                          0xDEu, 0xADu, 0xBEu, 0xEFu};
  std::this_thread::sleep_for(std::chrono::milliseconds(200));  // discovery
  ASSERT_EQ(h.publish(h.host_ctx, pub, msg, sizeof(msg)), 0);
  for (int i = 0; i < 50 && g_rx.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  EXPECT_EQ(g_rx.load(), 1);
  EXPECT_EQ(g_len, 8u);
  carla::ros2::TeardownExtensionEndpoints();
}
#endif  // CARLA_ROS2_MIDDLEWARE_CYCLONEDDS

#endif  // defined(WITH_ROS2)
