#pragma once

#include "Object/Object.h"
#include "Physics/Vehicle/VehiclePhysicsInterface.h"
#include "Source/Engine/Physics/Vehicle/VehicleWheel.generated.h"

UCLASS()
class UVehicleWheel : public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category = "Wheel Setup", DisplayName = "Bone Name")
	FName BoneName;

	UPROPERTY(Edit, Save, Category = "Wheel Setup", DisplayName = "Local Location", Type = Vec3)
	FVector LocalLocation = FVector::ZeroVector;

	UPROPERTY(Edit, Save, Category = "Wheel Setup", DisplayName = "Wheel Radius")
	float WheelRadius = 0.35f;

	UPROPERTY(Edit, Save, Category = "Wheel Setup", DisplayName = "Wheel Width")
	float WheelWidth = 0.25f;

	UPROPERTY(Edit, Save, Category = "Suspension", DisplayName = "Suspension Max Raise")
	float SuspensionMaxRaise = 0.15f;

	UPROPERTY(Edit, Save, Category = "Suspension", DisplayName = "Suspension Max Drop")
	float SuspensionMaxDrop = 0.25f;

	UPROPERTY(Edit, Save, Category = "Suspension", DisplayName = "Spring Rate")
	float SpringRate = 35000.0f;

	UPROPERTY(Edit, Save, Category = "Suspension", DisplayName = "Spring Damping")
	float SpringDamping = 4500.0f;

	UPROPERTY(Edit, Save, Category = "Traction", DisplayName = "Max Steer Angle")
	float MaxSteerAngle = 35.0f;

	UPROPERTY(Edit, Save, Category = "Traction", DisplayName = "Max Brake Torque")
	float MaxBrakeTorque = 4500.0f;

	UPROPERTY(Edit, Save, Category = "Traction", DisplayName = "Affected By Steering")
	bool bAffectedBySteering = false;

	UPROPERTY(Edit, Save, Category = "Traction", DisplayName = "Affected By Engine")
	bool bAffectedByEngine = false;

	UPROPERTY(Edit, Save, Category = "Traction", DisplayName = "Affected By Handbrake")
	bool bAffectedByHandbrake = false;

	FVehicleWheelConfig ToVehicleWheelConfig() const
	{
		FVehicleWheelConfig Config;
		Config.BoneName = BoneName;
		Config.LocalLocation = LocalLocation;
		Config.WheelRadius = WheelRadius;
		Config.WheelWidth = WheelWidth;
		Config.SuspensionMaxRaise = SuspensionMaxRaise;
		Config.SuspensionMaxDrop = SuspensionMaxDrop;
		Config.SpringRate = SpringRate;
		Config.SpringDamping = SpringDamping;
		Config.MaxSteerAngle = MaxSteerAngle;
		Config.MaxBrakeTorque = MaxBrakeTorque;
		Config.bAffectedBySteering = bAffectedBySteering;
		Config.bAffectedByEngine = bAffectedByEngine;
		Config.bAffectedByHandbrake = bAffectedByHandbrake;
		return Config;
	}
};
