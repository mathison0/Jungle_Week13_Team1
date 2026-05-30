#pragma once
#include "MovementComponent.h"


#include "Source/Engine/Component/Movement/WheeledVehicleMovementComponent.generated.h"

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

UCLASS()
class UWheeledVehicleMovementComponent : public UMovementComponent
{

public:
	GENERATED_BODY()
	UWheeledVehicleMovementComponent() = default;
	~UWheeledVehicleMovementComponent() override = default;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

private:
	//매 프레임(또는 물리 서브스텝)마다 바퀴의 본(Bone) 위치에서 아래 방향으로 지면을 탐색합니다.
	bool PerformOverlapChecks();
};

