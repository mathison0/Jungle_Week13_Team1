#pragma once

#include "Core/Types/CollisionTypes.h"
#include "Math/Vector.h"

#include <PxPhysicsAPI.h>
#include <vector>

class UPrimitiveComponent;

// ============================================================
// FPhysXSimulationCallback
//
// PhysX 의 onContact / onTrigger 는 Scene->fetchResults(true) 진행 중에 호출되며,
// 그 안에서 직접 게임 측 핸들러(NotifyComponentHit 등)를 호출하면 핸들러가
// World->DestroyActor 같은 scene-mutating 작업을 해서 fetchResults 와 겹쳐 크래쉬한다.
//
// 따라서 콜백은 이벤트를 큐에 적재만 하고, FPhysXPhysicsScene::Tick 의 post-simulate
// 단계 끝에서 DispatchPendingEvents 가 한꺼번에 게임 측 Notify 를 호출한다. 이 시점은
// simulate/fetchResults 외부이므로 핸들러가 자유롭게 actor/component 를 추가/제거해도 안전.
// ============================================================
class FPhysXSimulationCallback : public physx::PxSimulationEventCallback
{
public:
	// Block 접촉 -> 큐에 적재.
	void onContact(const physx::PxContactPairHeader& PairHeader,
		const physx::PxContactPair* Pairs, physx::PxU32 Count) override;

	// Trigger 진입/이탈 -> 큐에 적재.
	void onTrigger(physx::PxTriggerPair* Pairs, physx::PxU32 Count) override;

	// FPhysXPhysicsScene::Tick 끝에서 호출. simulate/fetchResults 바깥이므로 핸들러가
	// 자유롭게 World->DestroyActor / SpawnActor / RegisterComponent 호출 가능.
	// 핸들러 도중 다른 컴포넌트가 destroy되는 경우 대비해 dispatch 직전에 IsAliveObject
	// 검증 - destroy된 포인터를 만지지 않는다.
	void DispatchPendingEvents();

	void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override {}
	void onWake(physx::PxActor**, physx::PxU32) override {}
	void onSleep(physx::PxActor**, physx::PxU32) override {}
	void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, const physx::PxU32) override {}

private:
	struct FQueuedHit
	{
		UPrimitiveComponent* Self = nullptr;  // Notify 가 호출되는 대상
		UPrimitiveComponent* Other = nullptr;
		FVector NormalImpulse{0, 0, 0};
		FHitResult Hit;
		bool bBegin = true;  // false = end
	};

	struct FQueuedTrigger
	{
		UPrimitiveComponent* Self = nullptr;
		UPrimitiveComponent* Other = nullptr;
		bool bBegin = true;  // false = end
	};

	std::vector<FQueuedHit> PendingHits;
	std::vector<FQueuedTrigger> PendingTriggers;
};
