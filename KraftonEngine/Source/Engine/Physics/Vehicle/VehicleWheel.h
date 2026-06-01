#pragma once
#include "Object/Object.h"
#include "Engine/Physics/PhysicalMaterial.h"

#include "Source/Engine/Physics/Vehicle/UVehicleWheel.generated.h"

class PxVehicleTireData;
class PxVehicleDrivableSurfaceToTireFrictionPairs;


class FVehicleTireData
{
private:
	PxVehicleTireData PxTireData;
};

class FVehicleDrivableSurfaceToTireFrictionPairs
{
private:
	PxVehicleDrivableSurfaceToTireFrictionPairs PxSurfaceToTirePairs;
};

UCLASS()
class UVehicleWheel : public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(Edit, Save, Category = "PhysicalMaterial", DisplayName = "PhysicalMaterial")
	UPhysicalMaterial PhysicalMaterial;

	FVehicleTireData TireData;

	FVehicleDrivableSurfaceToTireFrictionPairs SurfaceToTirePairs;
	
	struct FWheelSetup
	{
		FName BoneName;
		float MaxSteerAngle;
		float WheelRadius;
		float WheelWidth;
	};

	struct FTraction
	{
		float MaxSteerAngle;
		float MaxBrakeTorque;
	};


};