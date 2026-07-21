// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "test.h"

#include <carla/ros2/publishers/ExtendedLidarPoint.h>
#include <carla/ros2/publishers/PointCloudFieldsLayout.h>

// ROS2.h is only compiled into carla-server (libcarla_test_server); guard the
// registration test so this TU stays a no-op on the client-side test binary.
// Same convention as test_ros2_topic_name.cpp / test_ros2_sensor_qos.cpp.
#if defined(WITH_ROS2)
#include "carla/ros2/ROS2.h"
#endif

#include <cstddef>
#include <cstdint>

namespace {

constexpr std::uint32_t SizeOf(carla::ros2::PointFieldDataType datatype) {
  switch (datatype) {
    case carla::ros2::PointFieldDataType::Int8:    return 1u;
    case carla::ros2::PointFieldDataType::UInt8:   return 1u;
    case carla::ros2::PointFieldDataType::Int16:   return 2u;
    case carla::ros2::PointFieldDataType::UInt16:  return 2u;
    case carla::ros2::PointFieldDataType::Int32:   return 4u;
    case carla::ros2::PointFieldDataType::UInt32:  return 4u;
    case carla::ros2::PointFieldDataType::Float32: return 4u;
    case carla::ros2::PointFieldDataType::Float64: return 8u;
  }
  return 0u;
}

}  // namespace

// The opt-in extended layout must expose exactly the canonical Autoware
// PointXYZIRCAEDT field order: x, y, z, intensity, return_type, channel,
// azimuth, elevation, distance, time_stamp.
TEST(extended_lidar, field_table_matches_tier4_setdataex_order) {
  const auto &f = carla::ros2::kLidarFieldsExtended;
  ASSERT_EQ(f.size(), 10u);
  const char *names[] = {"x", "y", "z", "intensity", "return_type", "channel",
                         "azimuth", "elevation", "distance", "time_stamp"};
  for (std::size_t i = 0; i < f.size(); ++i) {
    EXPECT_EQ(f[i].name, names[i]);
  }
  EXPECT_EQ(carla::ros2::ExtendedLidarPointStep(), 32u);
}

// Field offsets/datatypes must match the canonical 32-byte PointXYZIRCAEDT
// struct byte-for-byte (autoware/point_types/types.hpp). The layout is
// memcpy-compatible and Autoware's crop-box filter rejects anything else;
// naturally-aligned mixed types with no padding.
TEST(extended_lidar, field_offsets_describe_a_32_byte_point) {
  const auto &f = carla::ros2::kLidarFieldsExtended;
  EXPECT_EQ(f[0].offset, 0u);    // x FLOAT32
  EXPECT_EQ(f[3].offset, 12u);   // intensity
  EXPECT_EQ(f[3].datatype, carla::ros2::PointFieldDataType::UInt8);
  EXPECT_EQ(f[4].offset, 13u);   // return_type
  EXPECT_EQ(f[4].datatype, carla::ros2::PointFieldDataType::UInt8);
  EXPECT_EQ(f[5].offset, 14u);   // channel
  EXPECT_EQ(f[5].datatype, carla::ros2::PointFieldDataType::UInt16);
  EXPECT_EQ(f[6].offset, 16u);   // azimuth FLOAT32
  EXPECT_EQ(f[6].datatype, carla::ros2::PointFieldDataType::Float32);
  EXPECT_EQ(f[9].offset, 28u);   // time_stamp
  EXPECT_EQ(f[9].datatype, carla::ros2::PointFieldDataType::UInt32);
  const auto &last = f.back();
  EXPECT_EQ(last.offset + SizeOf(last.datatype) * last.count,
            carla::ros2::ExtendedLidarPointStep());
}

// The wire POD must pack to exactly point_step and its member offsets must line
// up with the field table, so a single memcpy of a LidarPointEx array is a
// valid PointCloud2 payload that Autoware can std::memcpy straight back into
// its 32-byte PointXYZIRCAEDT struct.
TEST(extended_lidar, wire_pod_packs_to_point_step) {
  using carla::ros2::LidarPointEx;
  EXPECT_EQ(sizeof(LidarPointEx), carla::ros2::ExtendedLidarPointStep());
  EXPECT_EQ(sizeof(LidarPointEx), 32u);
  EXPECT_EQ(offsetof(LidarPointEx, x), 0u);
  EXPECT_EQ(offsetof(LidarPointEx, intensity), 12u);
  EXPECT_EQ(offsetof(LidarPointEx, return_type), 13u);
  EXPECT_EQ(offsetof(LidarPointEx, channel), 14u);
  EXPECT_EQ(offsetof(LidarPointEx, azimuth), 16u);
  EXPECT_EQ(offsetof(LidarPointEx, elevation), 20u);
  EXPECT_EQ(offsetof(LidarPointEx, distance), 24u);
  EXPECT_EQ(offsetof(LidarPointEx, time_stamp), 28u);
}

// Handedness: CARLA/UE is left-handed (x forward, y right, z up); ROS is
// right-handed (x forward, y left, z up). The mapping is a single Y negation,
// and azimuth (measured about +z from +x) must flip sign with y to stay
// consistent. Elevation (about the xy-plane) and distance (a magnitude) are
// invariant under the flip.
TEST(extended_lidar, ros_frame_flip_negates_y_and_azimuth_only) {
  carla::ros2::LidarPointEx p{};
  p.x = 1.0f;
  p.y = 2.0f;
  p.z = 3.0f;
  p.intensity = 200u;
  p.return_type = 0u;
  p.channel = 7u;
  p.azimuth = 0.30f;
  p.elevation = -0.10f;
  p.distance = 12.5f;
  p.time_stamp = 1000u;

  carla::ros2::ApplyRosFrameFlip(p);

  EXPECT_FLOAT_EQ(p.x, 1.0f);
  EXPECT_FLOAT_EQ(p.y, -2.0f);           // y negated
  EXPECT_FLOAT_EQ(p.z, 3.0f);
  EXPECT_EQ(p.intensity, 200u);          // invariant
  EXPECT_FLOAT_EQ(p.azimuth, -0.30f);    // azimuth sign follows y
  EXPECT_FLOAT_EQ(p.elevation, -0.10f);  // elevation invariant
  EXPECT_FLOAT_EQ(p.distance, 12.5f);    // distance invariant
  EXPECT_EQ(p.channel, 7u);
  EXPECT_EQ(p.time_stamp, 1000u);
}

// The opt-in must never perturb the default 16-byte XYZI layout.
TEST(extended_lidar, default_layout_is_untouched) {
  ASSERT_EQ(carla::ros2::kLidarFields.size(), 4u);
  const auto &d = carla::ros2::kLidarFields.back();
  EXPECT_EQ(d.name, "intensity");
  EXPECT_EQ(d.offset + SizeOf(d.datatype) * d.count, 16u);
}

// CARLA's float intensity (attenuation factor, nominally [0, 1]) must quantize
// to the canonical UINT8 field as clamp(round(i * 255)).
TEST(extended_lidar, intensity_quantizes_to_uint8) {
  using carla::ros2::QuantizeIntensity;
  EXPECT_EQ(QuantizeIntensity(1.0f), 255u);
  EXPECT_EQ(QuantizeIntensity(0.5f), 128u);   // round(127.5) -> 128
  EXPECT_EQ(QuantizeIntensity(0.0f), 0u);
  EXPECT_EQ(QuantizeIntensity(2.0f), 255u);   // clamp above 1.0
  EXPECT_EQ(QuantizeIntensity(-0.5f), 0u);    // clamp below 0.0
}

#if defined(WITH_ROS2)
// The extended flag threads through RegisterSensor into the ActorRegistration,
// exactly like ros_topic_name / qos (Tasks 6-7), and defaults to false when the
// caller omits it.
TEST(extended_lidar, register_sensor_stores_extended_flag) {
  auto ros2 = carla::ros2::ROS2::GetInstance();

  int extended_actor = 0;
  ros2->RegisterSensor(&extended_actor, "lidar_top", "lidar_top", true,
                       "/sensing/lidar/top/pointcloud_raw_ex",
                       carla::ros2::PublisherQos::SensorData(), true);
  EXPECT_TRUE(ros2->LookupSensorExtendedForTest(&extended_actor));
  ros2->UnregisterSensor(&extended_actor);

  int default_actor = 0;
  ros2->RegisterSensor(&default_actor, "lidar_top2", "lidar_top2", true);
  EXPECT_FALSE(ros2->LookupSensorExtendedForTest(&default_actor));
  ros2->UnregisterSensor(&default_actor);
}
#endif  // defined(WITH_ROS2)
