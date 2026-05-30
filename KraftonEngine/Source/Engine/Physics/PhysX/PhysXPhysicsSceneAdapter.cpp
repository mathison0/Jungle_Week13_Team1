#include "PhysXPhysicsScene.h"

#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"
#include "Physics/BodyInstance.h"
#include "Physics/BodySetup.h"
#include "PhysXHelper.h"
#include "PhysXShapeDesc.h"

#include <PxPhysicsAPI.h>

#include <algorithm>
#include <memory>

using namespace physx;

// ================================================================
// PhysicsAsset / Ragdoll Adapter
//
// PhysicsAsset/Ragdoll 빌더(별도 담당)가 사용할 body 생성/해제 진입점.
// Actor 단위 compound(BodyMappings)와 달리 한 컴포넌트가 여러 독립 body를
// 가질 수 있으므로 FBodyInstance를 scene이 heap으로 소유한다.
//
// bone 순회 / ConstraintSetup 해석 / bone transform sync 같은 ragdoll 오케스트레이션은
// 이 어댑터의 책임이 아니다. 여기서는 "BodySetup 1개 -> PhysX body 1개" 변환과
// 그 생명주기 관리만 제공한다. joint는 CreateConstraint를 그대로 사용한다.
// ================================================================

FBodyInstance* FPhysXPhysicsScene::CreateBodyFromBodySetup(UPrimitiveComponent* OwnerComp, UBodySetup* BodySetup,
	const FTransform& WorldTransform, bool bDynamic)
{
	if (!Scene || !Physics || !DefaultMaterial || !BodySetup) return nullptr;

	const PxTransform Pose = FPhysXHelper::ToPxTransform(WorldTransform.Location, WorldTransform.Rotation);
	PxRigidActor* Actor = bDynamic
		? static_cast<PxRigidActor*>(Physics->createRigidDynamic(Pose))
		: static_cast<PxRigidActor*>(Physics->createRigidStatic(Pose));
	if (!Actor) return nullptr;

	auto Body = std::make_unique<FBodyInstance>();
	Body->InitBody(OwnerComp, Actor);
	FPhysXHelper::SetActorBodyRecord(Actor, Body.get());

	// collision: WorldDynamic, 모든 채널 Block. 같은 owner(컴포넌트)의 body끼리는
	// filter shader의 same-owner 가드로 충돌이 무시된다.
	const uint32 OwnerId = (OwnerComp && OwnerComp->GetOwner()) ? OwnerComp->GetOwner()->GetUUID() : 0;
	FPhysXShapeCollisionDesc Collision;
	Collision.CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	Collision.ObjectType = ECollisionChannel::WorldDynamic;
	Collision.Responses = FCollisionResponseContainer(ECollisionResponse::Block);
	Collision.OwnerActorId = OwnerId;
	Collision.bGenerateOverlapEvents = false;

	FPhysXShapeMaterialDesc Material; // override 없음 -> default material

	TArray<FPhysXShapeDesc> Descs;
	FPhysXShapeDescUtils::MakeShapeDescsFromBodySetupAsset(BodySetup,
		bDynamic ? EPhysXBodyType::Dynamic : EPhysXBodyType::Static, Collision, Material, Body.get(), Descs);

	bool bAnyShape = false;
	for (const FPhysXShapeDesc& Desc : Descs)
	{
		if (CreateShapeOnActor(Actor, Desc) != nullptr) bAnyShape = true;
	}

	if (!bAnyShape)
	{
		// shape가 없으면 body 의미가 없다. scene에 add 전이라 release만.
		FPhysXHelper::SetActorBodyRecord(Actor, nullptr);
		Actor->release();
		return nullptr;
	}

	// v1: dynamic body는 1kg 고정. 추후 density 기반으로 확장 가능.
	if (PxRigidDynamic* Dyn = Actor->is<PxRigidDynamic>())
	{
		PxRigidBodyExt::setMassAndUpdateInertia(*Dyn, 1.0f);
	}

	Scene->addActor(*Actor);

	FBodyInstance* Result = Body.get();
	AdapterBodies.push_back(std::move(Body));
	return Result;
}

void FPhysXPhysicsScene::DestroyBody(FBodyInstance* Body)
{
	if (!Body) return;

	// 이 body를 참조하는 joint를 먼저 해제한다 (PxRigidActor를 물고 있으므로).
	DestroyConstraintsForBody(Body);

	PxRigidActor* Actor = FPhysXHelper::GetRigidActor(Body);
	if (Actor)
	{
		FPhysXHelper::SetActorBodyRecord(Actor, nullptr);
		if (Scene) Scene->removeActor(*Actor);
	}
	Body->TerminateBody();
	if (Actor) Actor->release();

	AdapterBodies.erase(
		std::remove_if(AdapterBodies.begin(), AdapterBodies.end(),
			[Body](const std::unique_ptr<FBodyInstance>& Ptr) { return Ptr.get() == Body; }),
		AdapterBodies.end());
}
