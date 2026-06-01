#pragma once
#pragma once

#include "VehiclePhysicsInterface.h"
#include "Math/Transform.h"

// 휠 인덱스 상수 (PhysX 표준 순서)
// 0=FL, 1=FR, 2=RL, 3=RR
static constexpr physx::PxU32 NUM_WHEELS = 4;

class FPhysXVehicle4WPhysics : public IVehiclePhysics
{
public:
	FPhysXVehicle4WPhysics();
	virtual ~FPhysXVehicle4WPhysics() override;

	// ── IVehiclePhysics 오버라이드 ──────────────────────────────────────────
	virtual void CreateVehicle(physx::PxPhysics* physics,
		physx::PxScene* scene,
		physx::PxCooking* cooking) override;
	virtual void DestroyVehicle() override;
	virtual void SetThrottle(float value) override;
	virtual void SetBrake(float value) override;
	virtual void SetSteer(float value) override;
	virtual void TickVehicle(float deltaTime) override;

	virtual FTransform GetChassisTransform()          const override;
	virtual FTransform GetWheelTransform(int wheelIndex) const override;

};
