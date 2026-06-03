#include "PhysXSimulationCallback.h"

#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "Object/Object.h"
#include "Physics/BodyInstance.h"
#include "PhysXHelper.h"

using namespace physx;

void FPhysXSimulationCallback::onContact(const PxContactPairHeader& PairHeader,
	const PxContactPair* Pairs, PxU32 Count)
{
	if (PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_0
		|| PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_1)
		return;

	for (PxU32 i = 0; i < Count; ++i)
	{
		const PxContactPair& CP = Pairs[i];
		const bool bBegin = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND);
		const bool bEnd = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST);
		if (!bBegin && !bEnd) continue;

		FBodyInstance* BodyA = FPhysXHelper::GetBodyInstanceFromPxShape(CP.shapes[0]);
		FBodyInstance* BodyB = FPhysXHelper::GetBodyInstanceFromPxShape(CP.shapes[1]);
		UPrimitiveComponent* CompA = BodyA ? BodyA->GetOwnerComponent() : nullptr;
		UPrimitiveComponent* CompB = BodyB ? BodyB->GetOwnerComponent() : nullptr;
		if (!CompA || !CompB) continue;

		if (bEnd)
		{
			FQueuedHit A;
			A.Self = CompA;
			A.Other = CompB;
			A.bBegin = false;
			PendingHits.push_back(A);

			FQueuedHit B;
			B.Self = CompB;
			B.Other = CompA;
			B.bBegin = false;
			PendingHits.push_back(B);
			continue;
		}

		// Contact point - 큐 dispatch 시점에 PxContactPair 가 이미 무효이므로 여기서 모두 추출.
		PxContactPairPoint ContactPoints[1];
		PxU32 NumPoints = CP.extractContacts(ContactPoints, 1);

		FVector ContactPos(0, 0, 0);
		FVector ContactNormal(0, 0, 1);
		float Penetration = 0.0f;
		FVector ContactImpulse(0, 0, 0);

		if (NumPoints > 0)
		{
			ContactPos = FPhysXHelper::ToFVector(ContactPoints[0].position);
			ContactNormal = FPhysXHelper::ToFVector(ContactPoints[0].normal);
			Penetration = ContactPoints[0].separation; // 음수 = 관통
			ContactImpulse = FPhysXHelper::ToFVector(ContactPoints[0].impulse);
		}

		const FVector NormalImpulse = ContactImpulse;

		FQueuedHit A;
		A.Self = CompA;
		A.Other = CompB;
		A.NormalImpulse = NormalImpulse;
		A.Hit.bHit = true;
		A.Hit.HitComponent = CompB;
		A.Hit.HitActor = CompB->GetOwner();
		A.Hit.WorldHitLocation = ContactPos;
		A.Hit.ImpactNormal = ContactNormal;
		A.Hit.WorldNormal = ContactNormal;
		A.Hit.PenetrationDepth = -Penetration;
		PendingHits.push_back(A);

		FQueuedHit B;
		B.Self = CompB;
		B.Other = CompA;
		B.NormalImpulse = NormalImpulse * -1.0f;
		B.Hit.bHit = true;
		B.Hit.HitComponent = CompA;
		B.Hit.HitActor = CompA->GetOwner();
		B.Hit.WorldHitLocation = ContactPos;
		B.Hit.ImpactNormal = ContactNormal * -1.0f;
		B.Hit.WorldNormal = ContactNormal * -1.0f;
		B.Hit.PenetrationDepth = -Penetration;
		PendingHits.push_back(B);
	}
}

void FPhysXSimulationCallback::onTrigger(PxTriggerPair* Pairs, PxU32 Count)
{
	for (PxU32 i = 0; i < Count; ++i)
	{
		const PxTriggerPair& TP = Pairs[i];

		if (TP.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
			continue;

		FBodyInstance* TriggerBody = FPhysXHelper::GetBodyInstanceFromPxShape(TP.triggerShape);
		FBodyInstance* OtherBody = FPhysXHelper::GetBodyInstanceFromPxShape(TP.otherShape);
		UPrimitiveComponent* TriggerComp = TriggerBody ? TriggerBody->GetOwnerComponent() : nullptr;
		UPrimitiveComponent* OtherComp = OtherBody ? OtherBody->GetOwnerComponent() : nullptr;
		if (!TriggerComp || !OtherComp) continue;

		const bool bBegin = (TP.status == PxPairFlag::eNOTIFY_TOUCH_FOUND);
		const bool bEnd = (TP.status == PxPairFlag::eNOTIFY_TOUCH_LOST);
		if (!bBegin && !bEnd) continue;

		if (TriggerComp->GetGenerateOverlapEvents())
		{
			PendingTriggers.push_back({ TriggerComp, OtherComp, bBegin });
		}
		if (OtherComp->GetGenerateOverlapEvents())
		{
			PendingTriggers.push_back({ OtherComp, TriggerComp, bBegin });
		}
	}
}

void FPhysXSimulationCallback::DispatchPendingEvents()
{
	// move-out - dispatch 도중 새 이벤트가 큐에 들어오는 일은 없지만, 안전하게 swap 후 처리.
	std::vector<FQueuedHit> HitsToDispatch;
	HitsToDispatch.swap(PendingHits);
	std::vector<FQueuedTrigger> TriggersToDispatch;
	TriggersToDispatch.swap(PendingTriggers);

	for (FQueuedHit& E : HitsToDispatch)
	{
		if (!IsAliveObject(E.Self) || !IsAliveObject(E.Other)) continue;
		AActor* OtherActor = E.Other->GetOwner();
		if (E.bBegin)
		{
			E.Self->NotifyComponentHit(E.Self, OtherActor, E.Other, E.NormalImpulse, E.Hit);
		}
		else
		{
			E.Self->NotifyComponentEndHit(E.Self, OtherActor, E.Other);
		}
	}

	for (FQueuedTrigger& E : TriggersToDispatch)
	{
		if (!IsAliveObject(E.Self) || !IsAliveObject(E.Other)) continue;
		AActor* OtherActor = E.Other->GetOwner();
		if (E.bBegin)
		{
			FHitResult DummyHit;
			E.Self->NotifyComponentBeginOverlap(E.Self, OtherActor, E.Other, 0, false, DummyHit);
		}
		else
		{
			E.Self->NotifyComponentEndOverlap(E.Self, OtherActor, E.Other, 0);
		}
	}
}
