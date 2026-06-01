#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Types/CollisionTypes.h"
#include "Math/Vector.h"
#include "Object/FName.h"

class UPrimitiveComponent;
class UWorld;

constexpr int32 VehicleWheelCount4W = 4;

struct FVehicleWheelConfig
{
	FName BoneName;
	FVector LocalLocation = FVector::ZeroVector;

	float WheelRadius = 0.35f;
	float WheelWidth = 0.25f;
	float SuspensionMaxRaise = 0.15f;
	float SuspensionMaxDrop = 0.25f;
	float SpringRate = 35000.0f;
	float SpringDamping = 4500.0f;

	float MaxSteerAngle = 35.0f;
	float MaxBrakeTorque = 4500.0f;

	bool bAffectedBySteering = false;
	bool bAffectedByEngine = false;
	bool bAffectedByHandbrake = false;
};

struct FVehiclePhysicsConfig
{
	FVehicleWheelConfig Wheels[VehicleWheelCount4W];
	UPrimitiveComponent* WheelColliders[VehicleWheelCount4W] = {};

	float MaxDriveForce = 8500.0f;
	float TireFriction = 1.6f;
	float TireLateralStiffness = 12000.0f;
	float RollingResistance = 80.0f;
	ECollisionChannel GroundTraceChannel = ECollisionChannel::WorldStatic;
};

struct FVehicleWheelState
{
	bool bGrounded = false;
	FVector TraceStart = FVector::ZeroVector;
	FVector TraceEnd = FVector::ZeroVector;
	FVector WheelCenter = FVector::ZeroVector;
	FVector ContactPoint = FVector::ZeroVector;
	FVector ContactNormal = FVector::UpVector;
	FVector SuspensionForce = FVector::ZeroVector;
	FVector TireForce = FVector::ZeroVector;
	float Compression = 0.0f;
	float SteerAngle = 0.0f;
	float RotationAngle = 0.0f;
	float AngularVelocity = 0.0f;
};

class IVehiclePhysics
{
public:
	virtual ~IVehiclePhysics() = default;

	virtual void Initialize(UWorld* World, UPrimitiveComponent* ChassisComponent, const FVehiclePhysicsConfig& Config) = 0;
	virtual void DestroyVehicle() = 0;

	virtual void SetThrottle(float Value) = 0;
	virtual void SetBrake(float Value) = 0;
	virtual void SetSteer(float Value) = 0;
	virtual void SetHandbrake(float Value) = 0;
	virtual void SetWheelCollider(int32 WheelIndex, UPrimitiveComponent* WheelCollider) = 0;

	virtual void TickVehicle(float DeltaTime) = 0;

	virtual int32 GetWheelCount() const = 0;
	virtual const FVehicleWheelState& GetWheelState(int32 WheelIndex) const = 0;
};
