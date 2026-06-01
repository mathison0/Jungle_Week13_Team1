#pragma once

#include "PxPhysicsAPI.h"

class IVehiclePhysics
{
public:
	virtual ~IVehiclePhysics() = default;

	virtual void CreateVehicle(physx::PxPhysics* physics,
		physx::PxScene* scene,
		physx::PxCooking* cooking) = 0;
	virtual void DestroyVehicle() = 0;
	virtual void SetThrottle(float Value) = 0;
	virtual void SetBrake(float Value) = 0;
	virtual void SetSteer(float Value) = 0;
	virtual void TickVehicle(float DeltaTime) = 0;

	// 상태 동기화 (렌더링용)
	virtual FTransform GetChassisTransform() const = 0;
	virtual FTransform GetWheelTransform(int wheelIndex) const = 0;
};
