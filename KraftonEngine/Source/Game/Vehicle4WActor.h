#pragma once

#include "GameFramework/AActor.h"

#include "Source/Game/Vehicle4WActor.generated.h"

class UBoxComponent;
class USphereComponent;
class UWheeledVehicleMovementComponent;

UCLASS()
class AVehicle4WActor : public AActor
{
public:
	GENERATED_BODY()

	AVehicle4WActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;

	UBoxComponent* GetChassisComponent() const { return ChassisComponent; }
	UWheeledVehicleMovementComponent* GetVehicleMovementComponent() const { return VehicleMovementComponent; }

private:
	void RebindComponents();

private:
	UBoxComponent* ChassisComponent = nullptr;
	USphereComponent* WheelColliders[4] = {};
	UWheeledVehicleMovementComponent* VehicleMovementComponent = nullptr;
};
