// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla/Sensor/InertialMeasurementUnit.h"
#include "Carla.h"
#include "Carla/Actor/ActorBlueprintFunctionLibrary.h"
#include "Carla/Game/CarlaStatics.h"
#include "Carla/Sensor/WorldObserver.h"
#include "Carla/Vehicle/CarlaWheeledVehicle.h"

#include <util/disable-ue4-macros.h>
#include "carla/geom/Math.h"
#include "carla/ros2/ROS2.h"
#include <util/enable-ue4-macros.h>

#include <limits>

// Based on OpenDRIVE's lon and lat
const FVector AInertialMeasurementUnit::CarlaNorthVector =
    FVector(0.0f, -1.0f, 0.0f);

AInertialMeasurementUnit::AInertialMeasurementUnit(
    const FObjectInitializer &ObjectInitializer)
  : Super(ObjectInitializer)
{
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.TickGroup = TG_PostPhysics;
  RandomEngine = CreateDefaultSubobject<URandomEngine>(TEXT("RandomEngine"));
  PrevLocation = { FVector::ZeroVector, FVector::ZeroVector };
  // Initialized to something hight to minimize the artifacts
  // when the initial values are unknown
  PrevDeltaTime = std::numeric_limits<float>::max();
}

FActorDefinition AInertialMeasurementUnit::GetSensorDefinition()
{
  return UActorBlueprintFunctionLibrary::MakeIMUDefinition();
}

void AInertialMeasurementUnit::Set(const FActorDescription &ActorDescription)
{
  Super::Set(ActorDescription);
  UActorBlueprintFunctionLibrary::SetIMU(ActorDescription, this);
}

void AInertialMeasurementUnit::SetOwner(AActor* OwningActor)
{
  Super::SetOwner(OwningActor);
}

// Returns the WORLD-frame angular velocity of Actor's physics body. The IMU
// shares this angular velocity rigidly with its parent; expressing it in the
// SENSOR frame is done at the caller with the sensor's own global rotation
// (mirroring ComputeAccelerometer), NOT here with the parent's.
static FVector FIMU_GetActorGlobalAngularVelocityInRadians(
    AActor &Actor)
{
  const auto RootComponent = Cast<UPrimitiveComponent>(Actor.GetRootComponent());
  return RootComponent != nullptr
      ? RootComponent->GetPhysicsAngularVelocityInRadians()
      : FVector::ZeroVector;
}

const carla::geom::Vector3D AInertialMeasurementUnit::ComputeAccelerometerNoise(
    const FVector &Accelerometer)
{
  // Normal (or Gaussian or Gauss) distribution will be used as noise function.
  // A mean of 0.0 is used as a first parameter, the standard deviation is
  // determined by the client
  constexpr float Mean = 0.0f;
  return carla::geom::Vector3D
  {
      (float)(Accelerometer.X + RandomEngine->GetNormalDistribution(Mean, StdDevAccel.X)),
      (float)(Accelerometer.Y + RandomEngine->GetNormalDistribution(Mean, StdDevAccel.Y)),
      (float)(Accelerometer.Z + RandomEngine->GetNormalDistribution(Mean, StdDevAccel.Z))
  };
}

const carla::geom::Vector3D AInertialMeasurementUnit::ComputeGyroscopeNoise(
    const FVector &Gyroscope)
{
  // Normal (or Gaussian or Gauss) distribution and a bias will be used as
  // noise function.
  // A mean of 0.0 is used as a first parameter.The standard deviation and the
  // bias are determined by the client
  constexpr float Mean = 0.0f;
  return carla::geom::Vector3D
  {
      (float)(Gyroscope.X + BiasGyro.X + RandomEngine->GetNormalDistribution(Mean, StdDevGyro.X)),
      (float)(Gyroscope.Y + BiasGyro.Y + RandomEngine->GetNormalDistribution(Mean, StdDevGyro.Y)),
      (float)(Gyroscope.Z + BiasGyro.Z + RandomEngine->GetNormalDistribution(Mean, StdDevGyro.Z))
  };
}

carla::geom::Vector3D AInertialMeasurementUnit::ComputeAccelerometer(
    const float DeltaTime)
{
  // Used to convert from UE4's cm to meters
  constexpr float TO_METERS = 1e-2;
  // Gravity is configured per-simulation on the game mode; fall back to
  // Earth's ~9.81 m/s^2 if the CARLA game mode is unavailable.
  ACarlaGameModeBase* GameMode = UCarlaStatics::GetGameMode(GetWorld());
  const float GRAVITY = (GameMode != nullptr) ? GameMode->IMUSensorGravity : 9.81f;

  // 2nd derivative of the polynomic (quadratic) interpolation
  // using the point in current time and two previous steps:
  // d2[i] = -2.0*(y1/(h1*h2)-y2/((h2+h1)*h2)-y0/(h1*(h2+h1)))
  const FVector CurrentLocation = GetActorLocation();

  const FVector Y2 = PrevLocation[0];
  const FVector Y1 = PrevLocation[1];
  const FVector Y0 = CurrentLocation;
  const float H1 = DeltaTime;
  const float H2 = PrevDeltaTime;

  const float H1AndH2 = H2 + H1;
  const FVector A = Y1 / ( H1 * H2 );
  const FVector B = Y2 / ( H2 * (H1AndH2) );
  const FVector C = Y0 / ( H1 * (H1AndH2) );
  FVector FVectorAccelerometer = TO_METERS * -2.0f * ( A - B - C );

  // Update the previous locations
  PrevLocation[0] = PrevLocation[1];
  PrevLocation[1] = CurrentLocation;
  PrevDeltaTime = DeltaTime;

  // Add gravitational acceleration
  FVectorAccelerometer.Z += GRAVITY;

  FQuat ImuRotation =
      GetRootComponent()->GetComponentTransform().GetRotation();
  FVectorAccelerometer = ImuRotation.UnrotateVector(FVectorAccelerometer);

  // Cast from FVector to our Vector3D to correctly send the data in m/s^2
  // and apply the desired noise function, in this case a normal distribution
  const carla::geom::Vector3D Accelerometer =
      ComputeAccelerometerNoise(FVectorAccelerometer);

  return Accelerometer;
}

carla::geom::Vector3D AInertialMeasurementUnit::ComputeGyroscope()
{
  check(GetOwner() != nullptr);
  // The IMU is rigidly attached, so it shares the owner's angular velocity;
  // the owner's physics body is queried because the sensor's own root
  // component does not simulate physics.
  const FVector GlobalAngularVelocity =
      FIMU_GetActorGlobalAngularVelocityInRadians(*GetOwner());

  // Express the world-frame rate in THIS sensor's frame via the sensor's
  // GLOBAL rotation, exactly as ComputeAccelerometer does. The previous
  // spelling took the OWNER-frame rate and applied RotateVector by the
  // RELATIVE mount rotation -- Rotate is the inverse of what expressing a
  // vector in the child frame requires, and the relative transform also
  // breaks for multi-level attachments. Both errors cancel for a 180-degree
  // mount flip (self-inverse), which is how this survived until a
  // flip-mounted IMU (the AWSIM-Labs tamagawa kit frame) was actually fused
  // by a localization stack.
  const FQuat SensorGlobalRotation =
      GetRootComponent()->GetComponentTransform().GetRotation();

  const FVector FVectorGyroscope =
      SensorGlobalRotation.UnrotateVector(GlobalAngularVelocity);

  // Cast from FVector to our Vector3D to correctly send the data in rad/s
  // and apply the desired noise function, in this case a normal distribution
  const carla::geom::Vector3D Gyroscope =
      ComputeGyroscopeNoise(FVectorGyroscope);

  return Gyroscope;
}

float AInertialMeasurementUnit::ComputeCompass()
{
  // Magnetometer: orientation with respect to the North in rad
  const FVector ForwVect = GetActorForwardVector().GetSafeNormal2D();
  const float DotProd = FVector::DotProduct(CarlaNorthVector, ForwVect);

  // We check if the dot product is higher than 1.0 due to numerical error
  if (DotProd >= 1.00f)
    return 0.0f;

  const float Compass = std::acos(DotProd);
  // Keep the angle between [0, 2pi)
  if (FVector::CrossProduct(CarlaNorthVector, ForwVect).Z < 0.0f)
    return carla::geom::Math::Pi2<float>() - Compass;

  return Compass;
}

void AInertialMeasurementUnit::PostPhysTick(UWorld *World, ELevelTick TickType, float DeltaTime)
{
  TRACE_CPUPROFILER_EVENT_SCOPE(AInertialMeasurementUnit::PostPhysTick);
  AccelerometerValue = ComputeAccelerometer(DeltaTime);
  GyroscopeValue = ComputeGyroscope();
  CompassValue = ComputeCompass();

  auto DataStream = GetDataStream(*this);

  // ROS2
  #if defined(WITH_ROS2)
  auto ROS2 = carla::ros2::ROS2::GetInstance();
  if (ROS2->IsEnabled())
  {
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("ROS2 Send");
    auto StreamId = carla::streaming::detail::token_type(GetToken()).get_stream_id();
    AActor* ParentActor = GetAttachParentActor();
    if (ParentActor)
    {
      FTransform LocalTransformRelativeToParent = GetActorTransform().GetRelativeTransform(ParentActor->GetActorTransform());
      ROS2->ProcessDataFromIMU(DataStream.GetSensorType(), StreamId, LocalTransformRelativeToParent, AccelerometerValue, GyroscopeValue, CompassValue, this);
    }
    else
    {
      ROS2->ProcessDataFromIMU(DataStream.GetSensorType(), StreamId, DataStream.GetSensorTransform(), AccelerometerValue, GyroscopeValue, CompassValue, this);
    }
  }
  #endif

  {
    TRACE_CPUPROFILER_EVENT_SCOPE(AInertialMeasurementUnit::SerializeAndSend);
    DataStream.SerializeAndSend(*this, AccelerometerValue, GyroscopeValue, CompassValue);
  }
}

void AInertialMeasurementUnit::SetAccelerationStandardDeviation(const FVector &Vec)
{
  StdDevAccel = Vec;
}

void AInertialMeasurementUnit::SetGyroscopeStandardDeviation(const FVector &Vec)
{
  StdDevGyro = Vec;
}

void AInertialMeasurementUnit::SetGyroscopeBias(const FVector &Vec)
{
  BiasGyro = Vec;
}

const FVector &AInertialMeasurementUnit::GetAccelerationStandardDeviation() const
{
  return StdDevAccel;
}

const FVector &AInertialMeasurementUnit::GetGyroscopeStandardDeviation() const
{
  return StdDevGyro;
}

const FVector &AInertialMeasurementUnit::GetGyroscopeBias() const
{
  return BiasGyro;
}

const carla::geom::Vector3D& AInertialMeasurementUnit::GetAccelerometerValue() const
{
  return AccelerometerValue;
}

const carla::geom::Vector3D& AInertialMeasurementUnit::GetGyroscopeValue() const
{
  return GyroscopeValue;
}

float AInertialMeasurementUnit::GetCompassValue() const
{
  return CompassValue;
}

void AInertialMeasurementUnit::BeginPlay()
{
  Super::BeginPlay();
}
