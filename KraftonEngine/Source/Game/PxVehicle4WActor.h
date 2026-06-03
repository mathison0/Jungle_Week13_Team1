#pragma once

#include "GameFramework/Pawn/Pawn.h"

#include "Source/Game/PxVehicle4WActor.generated.h"

class UBoxComponent;
class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UPxVehicleMovementComponent;

// ======================================================
// APxVehicle4WActor
//
// PxVehicle 기반 4륜 차량 데모 액터. Box 섀시(물리 강체) + Cylinder StaticMesh
// 바퀴 4개(시각 전용, 충돌 없음) + UPxVehicleMovementComponent로 구성한다.
// ======================================================
UCLASS()
class APxVehicle4WActor : public APawn
{
public:
	GENERATED_BODY()

	APxVehicle4WActor() = default;

	void InitDefaultComponents();
	void PostDuplicate() override;

	UBoxComponent* GetChassisComponent()						const { return ChassisComponent; }
	UPxVehicleMovementComponent* GetVehicleMovementComponent()	const { return VehicleMovementComponent; }
	USpringArmComponent* GetSpringArm()							const { return SpringArm; }
	UCameraComponent* GetCamera()								const { return Camera; }

private:
	void RebindComponents();

	UBoxComponent* ChassisComponent = nullptr;
	UStaticMeshComponent* BodyMesh = nullptr;
	UStaticMeshComponent* WheelMeshes[4] = {};
	UPxVehicleMovementComponent* VehicleMovementComponent = nullptr;
	USpringArmComponent* SpringArm = nullptr;
	UCameraComponent* Camera = nullptr;
};
