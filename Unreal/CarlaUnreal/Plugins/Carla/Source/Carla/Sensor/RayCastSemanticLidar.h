// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.



#pragma once

#include "Carla/Sensor/Sensor.h"
#include "Carla/Actor/ActorDefinition.h"
#include "Carla/Sensor/LidarDescription.h"
#include "Carla/Actor/ActorBlueprintFunctionLibrary.h"

#include <util/disable-ue4-macros.h>
#include <carla/sensor/data/SemanticLidarData.h>
#include <util/enable-ue4-macros.h>

#include "RayCastSemanticLidar.generated.h"

/// A ray-cast based Lidar sensor.
UCLASS()
class CARLA_API ARayCastSemanticLidar : public ASensor
{
  GENERATED_BODY()

protected:

  using FSemanticLidarData = carla::sensor::data::SemanticLidarData;
  using FSemanticDetection = carla::sensor::data::SemanticLidarDetection;

  /// A recorded ray hit plus the commanded scan angles (degrees) it was shot
  /// at. The angles are threaded from SimulateLidar (where they were previously
  /// computed and discarded) so the opt-in extended (PointXYZIRCAEDT) lidar
  /// layout can emit per-point azimuth/elevation. The plain and semantic paths
  /// ignore the angles; only ARayCastLidar / AHSSLidar consume them when their
  /// LidarData is in extended mode.
  struct FRayCastHit {
    FHitResult HitResult;
    float Azimuth = 0.0f;    // commanded horizontal angle (deg)
    float Elevation = 0.0f;  // commanded vertical angle (deg), = LaserAngles[channel]
  };

public:
  static FActorDefinition GetSensorDefinition();

  ARayCastSemanticLidar(const FObjectInitializer &ObjectInitializer);

  virtual void Set(const FActorDescription &Description) override;
  virtual void Set(const FLidarDescription &LidarDescription);

protected:
  virtual void PostPhysTick(UWorld *World, ELevelTick TickType, float DeltaTime) override;

  /// Creates a Laser for each channel.
  void CreateLasers();

  /// Updates LidarMeasurement with the points read in DeltaTime.
  void SimulateLidar(const float DeltaTime);

  /// Shoot a laser ray-trace, return whether the laser hit something.
  bool ShootLaser(const float VerticalAngle, float HorizontalAngle, FHitResult &HitResult, FCollisionQueryParams& TraceParams) const;

  /// Method that allow to preprocess if the rays will be traced.
  virtual void PreprocessRays(uint32_t Channels, uint32_t MaxPointsPerChannel);

  /// Compute all raw detection information
  void ComputeRawDetection(const FHitResult &HitInfo, const FTransform &SensorTransf, FSemanticDetection &Detection) const;

  /// Saving the hits the raycast returns per channel, together with the
  /// commanded scan angles (degrees) of the ray that produced the hit.
  void WritePointAsync(uint32_t Channel, FHitResult &Detection, float Azimuth, float Elevation);

  /// Clear the recorded data structure
  void ResetRecordedHits(uint32_t Channels, uint32_t MaxPointsPerChannel);

  /// This method uses all the saved FHitResults, compute the
  /// RawDetections and then send it to the LidarData structure.
  virtual void ComputeAndSaveDetections(const FTransform &SensorTransform);

  UPROPERTY(EditAnywhere)
  FLidarDescription Description;

  TArray<float> LaserAngles;

  std::vector<std::vector<FRayCastHit>> RecordedHits;
  std::vector<std::vector<bool>> RayPreprocessCondition;
  std::vector<uint32_t> PointsPerChannel;

  /// Opt-in 10-float PointXYZIRCAEDT layout (blueprint attribute
  /// ros2_extended_lidar). Read from the actor description in the leaf
  /// sensors' Set() and forwarded to their LidarData; declared here because
  /// both ARayCastLidar and AHSSLidar (which own the extended LidarData) derive
  /// from this base. ARayCastSemanticLidar itself never reads it.
  bool bExtendedLidar = false;

private:
  FSemanticLidarData SemanticLidarData;

};
