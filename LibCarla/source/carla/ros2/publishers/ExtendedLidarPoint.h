// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace carla {
namespace ros2 {

// Wire POD for the opt-in extended lidar layout (blueprint attribute
// ros2_extended_lidar). This is the CANONICAL Autoware PointXYZIRCAEDT struct,
// byte-for-byte: it mirrors autoware/point_types/types.hpp
//   float x, y, z; uint8 intensity; uint8 return_type; uint16 channel;
//   float azimuth, elevation, distance; uint32 time_stamp;   // 32 bytes
// in tier4 SetDataEx order.
//
// The canonical 32-byte layout is MANDATORY, not stylistic. Autoware's sensing
// chain begins with the crop-box filter, which runtime-validates the incoming
// PointCloud2 layout and aborts otherwise:
//   "The pointcloud layout is not compatible with PointXYZIRCAEDT or
//    PointXYZIRC. Aborting"
// (abort string in libcrop_box_filter_node.so). The consumer std::memcpy's each
// point's bytes straight into the PointXYZIRCAEDT struct, so a wider/mixed
// encoding (e.g. FLOAT32 intensity or FLOAT64 time_stamp) is rejected at the
// first node. Hence intensity is UINT8, time_stamp is UINT32, and the point is
// exactly 32 B.
//
// Naturally aligned with no padding (channel @14 is 2-byte aligned, floats
// @16/20/24 are 4-byte aligned, time_stamp @28 is 4-byte aligned, size 32 is a
// multiple of 4). #pragma pack(1) is kept only to make "no padding" an enforced
// invariant rather than an incidental property of these particular offsets.
//
// Deliberately DDS-free (only <cmath>/<cstddef>/<cstdint>): shared by ROS2.cpp
// (carla-server, assembles the buffer from LidarData), CarlaLidarPublisher.cpp
// (libcarla-ros2-native.so, applies the frame flip) and the layout unit test.
// See the two-build-split note atop ROS2.cpp.
#pragma pack(push, 1)
struct LidarPointEx {
  float         x;
  float         y;
  float         z;
  std::uint8_t  intensity;
  std::uint8_t  return_type;
  std::uint16_t channel;
  float         azimuth;
  float         elevation;
  float         distance;
  std::uint32_t time_stamp;
};
#pragma pack(pop)

static_assert(sizeof(LidarPointEx) == 32u,
    "extended lidar point must be the canonical 32-byte PointXYZIRCAEDT");
static_assert(offsetof(LidarPointEx, x) == 0u,            "x offset");
static_assert(offsetof(LidarPointEx, y) == 4u,            "y offset");
static_assert(offsetof(LidarPointEx, z) == 8u,            "z offset");
static_assert(offsetof(LidarPointEx, intensity) == 12u,   "intensity offset");
static_assert(offsetof(LidarPointEx, return_type) == 13u, "return_type offset");
static_assert(offsetof(LidarPointEx, channel) == 14u,     "channel offset");
static_assert(offsetof(LidarPointEx, azimuth) == 16u,     "azimuth offset");
static_assert(offsetof(LidarPointEx, elevation) == 20u,   "elevation offset");
static_assert(offsetof(LidarPointEx, distance) == 24u,    "distance offset");
static_assert(offsetof(LidarPointEx, time_stamp) == 28u,  "time_stamp offset");

// Quantize CARLA's float lidar intensity (an attenuation factor, nominally in
// [0, 1]) into the canonical UINT8 intensity field: clamp(round(i * 255)) to
// [0, 255]. Matches how AWSIM/tier4 report reflectivity as a 0-255 byte.
inline std::uint8_t QuantizeIntensity(float intensity) noexcept {
  const float scaled = std::round(intensity * 255.0f);
  if (scaled <= 0.0f) {
    return 0u;
  }
  if (scaled >= 255.0f) {
    return 255u;
  }
  return static_cast<std::uint8_t>(scaled);
}

// Maps a point from CARLA/UE's left-handed frame (x forward, y right, z up) to
// the ROS right-handed frame (x forward, y left, z up). The Cartesian mapping
// is a single Y negation (the same one the legacy 16-byte path applies in
// CarlaLidarPublisher::ComputePointCloud). Because azimuth is measured about
// +z from +x, its sign must flip together with y (azimuth_ros =
// -azimuth_carla); otherwise the reported bearing would disagree with the
// point's actual y after the flip. Elevation (measured off the xy-plane, which
// the Y negation leaves in place) and distance (a magnitude) are invariant.
inline void ApplyRosFrameFlip(LidarPointEx &p) noexcept {
  p.y = -p.y;
  p.azimuth = -p.azimuth;
}

}  // namespace ros2
}  // namespace carla
