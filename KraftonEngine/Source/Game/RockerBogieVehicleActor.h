#pragma once

#include "GameFramework/Pawn/Pawn.h"

#include "Source/Game/RockerBogieVehicleActor.generated.h"

class UBoxComponent;
class UCameraComponent;
class URockerBogieVehicleMovementComponent;
class USpringArmComponent;
class UStaticMeshComponent;

UCLASS()
class ARockerBogieVehicleActor : public APawn
{
public:
	GENERATED_BODY()

	ARockerBogieVehicleActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;

private:
	void RebindComponents();

	UBoxComponent* ChassisComponent = nullptr;
	UStaticMeshComponent* BodyMesh = nullptr;
	UStaticMeshComponent* WheelMeshes[6] = {};
	UStaticMeshComponent* LinkMeshes[4] = {};
	URockerBogieVehicleMovementComponent* VehicleMovementComponent = nullptr;
	USpringArmComponent* SpringArm = nullptr;
	UCameraComponent* Camera = nullptr;
};
