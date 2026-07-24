// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "test.h"

#include <carla/ros2/publishers/ImuMath.h>

// The IMU wire contract (sensor_msgs/Imu) is REP-103: right-handed, X forward
// / Y left / Z up in the frame named by header.frame_id. UE computes the
// sensor-frame values in its left-handed X forward / Y right / Z up frame, so
// every component set crossing that boundary needs the handedness conversion:
//
//   - linear acceleration is a POLAR vector: the Y axis flips        -> (x, -y, z)
//   - angular velocity is a PSEUDOVECTOR: it picks up an extra
//     overall sign on top of the axis flip                           -> (-x, y, -z)
//
// CarlaIMUPublisher::Write historically copied the UE components VERBATIM.
// For a flip-mounted IMU (the AWSIM-Labs tamagawa/imu_link, ~180deg mount)
// that inverted the fused yaw rate once Autoware's imu_corrector rotated the
// sample by the mount calibration: the EKF yaw then MIRRORED the physical
// rotation and the lateral controller ran away to full lock within ~25 m,
// reproducibly crashing closed-loop Autoware drives. The live boundary
// measurement pinned the contract these tests encode: during a physical RIGHT
// turn the wire angular_velocity.z must be POSITIVE (flipped sensor frame),
// and the corrected base_link value must equal the ground-truth yaw rate.

namespace {

constexpr float kEps = 1e-6f;

}  // namespace

TEST(ImuAxes, linear_acceleration_flips_y_only) {
  const auto v = carla::ros2::LinearUEToRos(1.0f, 2.0f, 3.0f);
  ASSERT_NEAR(v[0], 1.0f, kEps);
  ASSERT_NEAR(v[1], -2.0f, kEps);
  ASSERT_NEAR(v[2], 3.0f, kEps);
}

TEST(ImuAxes, angular_velocity_flips_x_and_z) {
  const auto w = carla::ros2::AngularUEToRos(1.0f, 2.0f, 3.0f);
  ASSERT_NEAR(w[0], -1.0f, kEps);
  ASSERT_NEAR(w[1], 2.0f, kEps);
  ASSERT_NEAR(w[2], -3.0f, kEps);
}

TEST(ImuAxes, right_turn_reaches_the_wire_positive_on_a_flipped_mount) {
  // The live-measured G2 failure case. Vehicle turning RIGHT at |w| rad/s:
  // UE vehicle yaw rate is +|w| (left-handed, clockwise-positive); the 180deg
  // mount flip (diag(-1, +1, -1)) makes the UE SENSOR-frame z rate -|w|. The
  // REP-103 wire value in the flipped sensor frame must be +|w| so that
  // imu_corrector's rotation by the mount calibration recovers the true
  // base_link yaw rate of -|w|. The verbatim copy shipped -|w| instead --
  // the sign inversion that crashed the G2 drives.
  const float w_abs = 0.319f;  // rad/s, from the live probe
  const float ue_sensor_z = -w_abs;
  const auto wire = carla::ros2::AngularUEToRos(0.0f, 0.0f, ue_sensor_z);
  ASSERT_NEAR(wire[2], +w_abs, kEps);
}

TEST(ImuAxes, gravity_on_a_flipped_mount_stays_negative_z_on_the_wire) {
  // Stationary, flip-mounted IMU: UE sensor-frame specific force is
  // (0, 0, -9.81) (the mount flip points sensor Z down). The wire value must
  // KEEP z = -9.81 -- the polar conversion only flips Y -- so that the mount
  // calibration rotation recovers +9.81 in base_link.
  const auto wire = carla::ros2::LinearUEToRos(0.0f, 0.0f, -9.81f);
  ASSERT_NEAR(wire[0], 0.0f, kEps);
  ASSERT_NEAR(wire[1], 0.0f, kEps);
  ASSERT_NEAR(wire[2], -9.81f, kEps);
}
