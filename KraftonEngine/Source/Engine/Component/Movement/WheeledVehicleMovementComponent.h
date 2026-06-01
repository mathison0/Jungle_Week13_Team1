#pragma once
#include "MovementComponent.h"
#include "Engine/Physics/Vehicle/VehicleWheel.h"


#include "Source/Engine/Component/Movement/WheeledVehicleMovementComponent.generated.h"


UCLASS()
class UWheeledVehicleMovementComponent : public UMovementComponent
{

public:
	GENERATED_BODY()
	UWheeledVehicleMovementComponent() = default;
	~UWheeledVehicleMovementComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:


private:
	UVehicleWheel Wheels[4];
};

