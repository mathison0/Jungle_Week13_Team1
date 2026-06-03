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
// 한 컴포넌트가 여러 독립 body를 가질 수 있으며(뼈마다 하나), 각 FBodyInstance의
// 소유권은 생성해 반환받은 컴포넌트가 가진다(unique_ptr).
//
// bone 순회 / ConstraintSetup 해석 / bone transform sync 같은 ragdoll 오케스트레이션은
// 이 어댑터의 책임이 아니다. 여기서는 "BodySetup 1개 -> PhysX body 1개" 변환과
// PhysX 자원 해제만 제공한다. joint는 CreateConstraint를 그대로 사용한다.
// ================================================================

std::unique_ptr<FBodyInstance> FPhysXPhysicsScene::CreateBodyFromBodySetup(
	UPrimitiveComponent* OwnerComp,
	UBodySetup* BodySetup,
	const FTransform& WorldTransform,
	bool bDynamic,
	float UniformScale)
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
	Collision.CollisionEnabled = OwnerComp ? OwnerComp->GetCollisionEnabled() : ECollisionEnabled::QueryAndPhysics;
	Collision.ObjectType = OwnerComp ? OwnerComp->GetCollisionObjectType() : ECollisionChannel::WorldDynamic;
	Collision.Responses = OwnerComp ? OwnerComp->GetCollisionResponseContainer() : FCollisionResponseContainer(ECollisionResponse::Block);
	Collision.OwnerActorId = OwnerId;
	Collision.bGenerateOverlapEvents = OwnerComp && OwnerComp->GetGenerateOverlapEvents();

	FPhysXShapeMaterialDesc Material; // override 없음 -> default material

	Material.OverrideMaterial = OwnerComp ? OwnerComp->GetPhysicalMaterialOverride() : nullptr;

	TArray<FPhysXShapeDesc> Descs;
	FPhysXShapeDescUtils::MakeShapeDescsFromBodySetupAsset(BodySetup,
		bDynamic ? EPhysXBodyType::Dynamic : EPhysXBodyType::Static, Collision, Material, Body.get(), UniformScale, Descs);

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

	// dynamic body는 1kg 고정. 추후 density 기반으로 확장 가능.
	if (PxRigidDynamic* Dyn = Actor->is<PxRigidDynamic>())
	{
		// 랙돌 안정화(raw dynamic body = PhysicsAsset/랙돌 경로):
		
		// 겹친물체 밀어내는 최대 속도 제한
		Dyn->setMaxDepenetrationVelocity(5.0f);
		// PhysX에서 Rigid Body의 충돌/조인트 제약을 몇 번 반복해서 풀지
		Dyn->setSolverIterationCounts(8, 2);

		const float MassKg = OwnerComp && OwnerComp->GetMass() > 0.0f ? OwnerComp->GetMass() : 1.0f;
		PxRigidBodyExt::setMassAndUpdateInertia(*Dyn, MassKg);
	}

	Scene->addActor(*Actor);

	// 소유권(unique_ptr)을 호출자(컴포넌트)에게 넘긴다.
	return Body;
}

// 강체 하나의 PhysX 자원을 해제하는 공통 경로. Shutdown / DestroyBody / DestroyPhysicsAssetBodies가 함께 쓴다.
// FBodyInstance 객체는 소유자(컴포넌트)가 지우므로 여기선 delete하지 않는다.
void FPhysXPhysicsScene::ReleaseBodyResource(FBodyInstance* Body)
{
	if (!Body) return;

	// 같은 강체에 합쳐진 다른 컴포넌트들의 body도 함께 정리한다(같은 강체를 공유하므로).
	for (UPrimitiveComponent* Comp : Body->CombinedComponents)
	{
		FBodyInstance* ChildBody = Comp ? Comp->GetBodyInstance() : nullptr;
		if (ChildBody && ChildBody != Body)
		{
			ChildBody->TerminateBody();
		}
	}

	PxRigidActor* Actor = FPhysXHelper::GetRigidActor(Body);
	if (Actor)
	{
		FPhysXHelper::SetActorBodyRecord(Actor, nullptr);
		if (Scene) Scene->removeActor(*Actor);
	}
	Body->TerminateBody();
	if (Actor) Actor->release();
}

void FPhysXPhysicsScene::DestroyBody(FBodyInstance* Body)
{
	// PhysX 자원(PxRigidActor)만 해제한다. FBodyInstance 객체는 소유자(컴포넌트 Bodies)가 삭제한다.
	ReleaseBodyResource(Body);
}
