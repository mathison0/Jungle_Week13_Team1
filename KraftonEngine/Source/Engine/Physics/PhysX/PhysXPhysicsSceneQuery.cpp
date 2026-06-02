#include "PhysXPhysicsScene.h"

#include "Component/PrimitiveComponent.h"
#include "Component/ShapeComponent.h"
#include "GameFramework/AActor.h"
#include "PhysXHelper.h"
#include "PhysXQueryUtils.h"

#include <PxPhysicsAPI.h>

using namespace physx;

// ============================================================
// Raycast
// ============================================================

bool FPhysXPhysicsScene::Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	++PendingRaycastQueries;
	if (!Scene) return false;

	// Channel + IgnoreActor 통합 filter.
	// shape의 queryFilterData는 SetupFilterData에서 word0=ObjectType, word1=Block 마스크.
	// 응답이 TraceChannel에 대해 Block(=word1의 해당 비트 set)인 shape만 hit으로 인정.
	// trigger flag가 set된 shape는 PhysX 측 query에서 자동 제외되므로 별도 처리 불필요.
	struct FChannelRaycastFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;

		FChannelRaycastFilter(const AActor* InIgnoreActor, ECollisionChannel InChannel)
			: IgnoreActor(InIgnoreActor)
			, TraceBit(1u << static_cast<PxU32>(InChannel))
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IgnoreActor && FPhysXHelper::GetOwnerActorFromPxActor(Actor) == IgnoreActor)
			{
				return PxQueryHitType::eNONE;
			}

			// shape의 응답이 TraceChannel에 대해 Block인지 확인.
			// (word1[TraceChannel 비트]가 set이면 Block 응답)
			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				if ((ShapeData.word1 & TraceBit) == 0)
				{
					return PxQueryHitType::eNONE;
				}
			}

			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxRaycastBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FChannelRaycastFilter FilterCallback(IgnoreActor, TraceChannel);

	bool bStatus = Scene->raycast(FPhysXHelper::ToPxVec3(Start), FPhysXHelper::ToPxVec3(Dir), MaxDist, Hit, PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxRaycastHit& Block = Hit.block;
	FPhysXQueryUtils::FillRaycastHit(Block, OutHit);

	return true;
}

bool FPhysXPhysicsScene::RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	uint32 ObjectTypeMask, const AActor* IgnoreActor) const
{
	++PendingRaycastQueries;
	if (!Scene || ObjectTypeMask == 0) return false;

	// SetupFilterData (line ~322) 에서 word0 = ObjectType (채널 enum 값) 으로 set.
	// ObjectType 마스크 비트 검사로 hit 후보 필터.
	// Trigger flag shape 는 PhysX 측 query 단계에서 자동 제외.
	struct FObjectTypeRaycastFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 ObjectTypeMask = 0;

		FObjectTypeRaycastFilter(const AActor* InIgnoreActor, PxU32 InMask)
			: IgnoreActor(InIgnoreActor)
			, ObjectTypeMask(InMask)
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IgnoreActor && FPhysXHelper::GetOwnerActorFromPxActor(Actor) == IgnoreActor)
			{
				return PxQueryHitType::eNONE;
			}
			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				const PxU32 ShapeObjectBit = 1u << ShapeData.word0;
				if ((ShapeObjectBit & ObjectTypeMask) == 0)
				{
					return PxQueryHitType::eNONE;
				}
			}
			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxRaycastBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FObjectTypeRaycastFilter FilterCallback(IgnoreActor, ObjectTypeMask);

	bool bStatus = Scene->raycast(FPhysXHelper::ToPxVec3(Start), FPhysXHelper::ToPxVec3(Dir), MaxDist, Hit, PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxRaycastHit& Block = Hit.block;
	FPhysXQueryUtils::FillRaycastHit(Block, OutHit);

	return true;
}

bool FPhysXPhysicsScene::SphereSweepShapeComponents(const FVector& Start, const FVector& Dir, float MaxDist, float Radius,
	FHitResult& OutHit, ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	++PendingSweepQueries;
	if (!Scene || MaxDist <= 0.0f) return false;

	struct FShapeChannelSweepFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;

		FShapeChannelSweepFilter(const AActor* InIgnoreActor, ECollisionChannel InChannel)
			: IgnoreActor(InIgnoreActor)
			, TraceBit(1u << static_cast<PxU32>(InChannel))
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IgnoreActor && FPhysXHelper::GetOwnerActorFromPxActor(Actor) == IgnoreActor)
			{
				return PxQueryHitType::eNONE;
			}

			FBodyInstance* Body = FPhysXHelper::GetBodyInstanceFromPxShape(Shape);
			UPrimitiveComponent* Comp = Body ? Body->GetOwnerComponent() : nullptr;
			if (!Comp || !Cast<UShapeComponent>(Comp))
			{
				return PxQueryHitType::eNONE;
			}

			const PxFilterData ShapeData = Shape->getQueryFilterData();
			if ((ShapeData.word1 & TraceBit) == 0)
			{
				return PxQueryHitType::eNONE;
			}

			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FShapeChannelSweepFilter FilterCallback(IgnoreActor, TraceChannel);

	if (Radius <= 0.0f)
	{
		PxRaycastBuffer RayHit;
		const bool bStatus = Scene->raycast(FPhysXHelper::ToPxVec3(Start), FPhysXHelper::ToPxVec3(Dir), MaxDist, RayHit,
			PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
		if (!bStatus || !RayHit.hasBlock) return false;

		const PxRaycastHit& Block = RayHit.block;
		FPhysXQueryUtils::FillRaycastHit(Block, OutHit);
		OutHit.WorldHitLocation = Start + Dir * Block.distance;

		return true;
	}

	PxSweepBuffer Hit;
	const PxSphereGeometry SweepGeometry(Radius);
	const PxTransform StartPose(FPhysXHelper::ToPxVec3(Start));
	const bool bStatus = Scene->sweep(SweepGeometry, StartPose, FPhysXHelper::ToPxVec3(Dir), MaxDist, Hit,
		PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxSweepHit& Block = Hit.block;
	FPhysXQueryUtils::FillSweepHit(Block, Start, Dir, OutHit);

	return true;
}
