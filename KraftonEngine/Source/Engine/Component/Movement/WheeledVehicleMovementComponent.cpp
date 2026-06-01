#include "WheeledVehicleMovementComponent.h"


void UWheeledVehicleMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
/*
	
	
처음에는 TickComponent에서 구현해도 됨
하지만 나중에는 PhysicsScene Tick/Substep 안쪽으로 VehicleSimulation을 넣는 게 더 안정적

*/
	//1. Input 처리

	//2. 지면 감지

	//3. 서스펜션 힘 계산

	//4. 토크 적용 및 바퀴 회전 속도

	//5. Traction & Steering

	//6. Apply forces to chassis
}

