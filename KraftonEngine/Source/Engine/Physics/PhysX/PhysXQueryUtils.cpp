#include "PhysXQueryUtils.h"

#include "Physics/BodyInstance.h"
#include "PhysXHelper.h"

using namespace physx;

static void FillHitOwner(const PxShape* Shape, const PxRigidActor* Actor, FHitResult& OutHit)
{
	if (FBodyInstance* HitBody = FPhysXHelper::GetBodyInstanceFromPxShape(Shape))
	{
		OutHit.HitComponent = HitBody->GetOwnerComponent();
		OutHit.HitActor = HitBody->GetOwnerActor();
	}
	else
	{
		OutHit.HitComponent = FPhysXHelper::GetOwnerComponentFromPxActor(Actor);
		OutHit.HitActor = FPhysXHelper::GetOwnerActorFromPxActor(Actor);
	}
}

void FPhysXQueryUtils::FillRaycastHit(const PxRaycastHit& Block, FHitResult& OutHit)
{
	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = FPhysXHelper::ToFVector(Block.position);
	OutHit.ImpactNormal = FPhysXHelper::ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;
	FillHitOwner(Block.shape, Block.actor, OutHit);
}

void FPhysXQueryUtils::FillSweepHit(const PxSweepHit& Block, const FVector& Start, const FVector& Dir, FHitResult& OutHit)
{
	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = Start + Dir * Block.distance;
	OutHit.ImpactNormal = FPhysXHelper::ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;
	FillHitOwner(Block.shape, Block.actor, OutHit);
}
