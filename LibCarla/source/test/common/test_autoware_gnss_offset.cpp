// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "test.h"

#include <carla/ros2/publishers/TransformQuaternion.h>

// AutowareGNSSPublisher::Write publishes
//   pose = TransformFromCarlaRotation(tx, ty, tz, ...).translation + mgrs_offset
// so a CARLA world point (x, y, z) in metres lands at
//   (X + x, Y - y, Z + z)
// in the Autoware map frame for an MGRS offset (X, Y, Z) -- the offset is
// added AFTER the CARLA->ROS Y mirror. The level data asset, the
// mgrs_offset_* blueprint fallback, run_carla_autoware.sh --map-origin and
// the E2E gates all share that affine.
//
// What these tests pin: the CARLA->ROS Y mirror performed by
// TransformFromCarlaRotation, which is shared code called directly below,
// and the Nishi-Shinjuku golden offsets the consumers named above all
// share.
//
// What they do NOT pin: PublishedPose() below REPRODUCES Write()'s
// composition rather than invoking it, so a reordering inside Write() --
// mirroring after adding, or negating the offset's y -- would leave these
// tests green. Catching that needs a change to the publisher itself, which
// is out of scope for this sensor-side fallback.
namespace {

struct MapPose { double x, y, z; };

MapPose PublishedPose(float x, float y, float z, double ox, double oy, double oz) {
  const auto tq = carla::ros2::TransformFromCarlaRotation(x, y, z, 0.0f, 0.0f, 0.0f);
  return {tq.translation[0] + ox, tq.translation[1] + oy, tq.translation[2] + oz};
}

}  // namespace

TEST(AutowareGnssOffset, zero_offset_is_a_pure_y_mirror) {
  const MapPose p = PublishedPose(10.0f, 5.0f, 2.0f, 0.0, 0.0, 0.0);
  EXPECT_NEAR(p.x, 10.0, 1e-6);
  EXPECT_NEAR(p.y, -5.0, 1e-6);
  EXPECT_NEAR(p.z, 2.0, 1e-6);
}

TEST(AutowareGnssOffset, nishi_golden_offsets_after_the_mirror) {
  // Nishi-Shinjuku: converter offset (81655.73, 50137.43, 42.49998) m, MGRS 54SUE.
  // CARLA goal (-84.114, 117.603) must publish as the lanelet-226 goal
  // (81571.616, 50019.827) recorded in the extension repository.
  const MapPose p = PublishedPose(-84.114f, 117.603f, 0.0f, 81655.73, 50137.43, 42.49998);
  EXPECT_NEAR(p.x, 81571.616, 1e-3);
  EXPECT_NEAR(p.y, 50019.827, 1e-3);
  EXPECT_NEAR(p.z, 42.49998, 1e-6);
}

TEST(AutowareGnssOffset, world_origin_publishes_as_the_offset_itself) {
  const MapPose p = PublishedPose(0.0f, 0.0f, 0.0f, 81655.73, 50137.43, 42.49998);
  EXPECT_NEAR(p.x, 81655.73, 1e-9);
  EXPECT_NEAR(p.y, 50137.43, 1e-9);
  EXPECT_NEAR(p.z, 42.49998, 1e-9);
}
