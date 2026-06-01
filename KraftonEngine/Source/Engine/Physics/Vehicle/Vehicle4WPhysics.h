#pragma once

#include "Physics/Vehicle/VehiclePhysicsInterface.h"

class FPhysXVehicle4WPhysics : public IVehiclePhysics
{
public:
	FPhysXVehicle4WPhysics();
	~FPhysXVehicle4WPhysics() override;

	void Initialize(UWorld* World, UPrimitiveComponent* ChassisComponent, const FVehiclePhysicsConfig& Config) override;
	void DestroyVehicle() override;

	void SetThrottle(float Value) override;
	void SetBrake(float Value) override;
	void SetSteer(float Value) override;
	void SetHandbrake(float Value) override;
	void SetWheelCollider(int32 WheelIndex, UPrimitiveComponent* WheelCollider) override;

	void TickVehicle(float DeltaTime) override;

	int32 GetWheelCount() const override { return VehicleWheelCount4W; }
	const FVehicleWheelState& GetWheelState(int32 WheelIndex) const override;

private:
	FVector TransformLocalToWorld(const FVector& Local) const;
	FVector RotateAroundUp(const FVector& Vector, float AngleRadians) const;
	void SimulateWheel(int32 WheelIndex, float DeltaTime);
	void ResetRuntimeState();

private:
	UWorld* World = nullptr;
	UPrimitiveComponent* ChassisComponent = nullptr;
	FVehiclePhysicsConfig Config;
	UPrimitiveComponent* WheelColliders[VehicleWheelCount4W] = {};
	FVehicleWheelState WheelStates[VehicleWheelCount4W];
	float PreviousCompression[VehicleWheelCount4W] = {};

	float ThrottleInput = 0.0f;
	float BrakeInput = 0.0f;
	float SteerInput = 0.0f;
	float HandbrakeInput = 0.0f;
};
