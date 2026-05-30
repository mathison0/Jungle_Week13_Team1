#include "WheeledVehicleMovementComponent.h"
#include "Physics/Vehicle/SuspensionSystem.h"

void UWheeledVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	//1. `PerformOverlap`을 통해 지면 감지 및 서스펜션 압축량 확인.
	bool bOverlapDetected = PerformOverlapChecks();

	//2. 지면 감지 결과로 얻은 거리 데이터를 바탕으로 차체를 밀어올리는 힘을 계산합
	float SuspensionForce = FSuspensionSystem::CalculateSuspensionForce(/*FSuspensionSetup*/{}, /*CompressionRatio*/0.0f, /*WheelVelocity*/FVector::ZeroVector, /*ContactNormal*/FVector::UpVector);
	
	//3. 엔진 출력, 브레이크, 관성에 의한 바퀴 자체의 회전 속도를 계산합니다.

	//4. 조향각과 슬립 데이터를 조합해 지면 마찰력(Traction) 산출.

	//5. 계산된 모든 힘을 Chaos 물리 엔진의 `RigidBody`에 최종 전달.
}

bool UWheeledVehicleMovementComponent::PerformOverlapChecks()
{
	return false;
}

