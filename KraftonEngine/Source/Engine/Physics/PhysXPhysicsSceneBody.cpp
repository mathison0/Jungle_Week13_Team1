#include "Physics/PhysXPhysicsScene.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "GameFramework/AActor.h"
#include "Core/Logging/Log.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysXCollision.h"
#include "Physics/PhysXHelper.h"

#include <PxPhysicsAPI.h>

#include <vector>

using namespace physx;

static PxMaterial* TryGetOrCreatePxMaterial(UPrimitiveComponent* Comp, UPhysicalMaterial* DefaultPhysicalMaterial, PxMaterial* DefaultMaterial, PxPhysics* Physics)
{
	if (!Physics) return DefaultMaterial;

	if (Comp)
	{
		if (UPhysicalMaterial* OverrideMaterial = Comp->GetPhysicalMaterialOverride())
		{
			if (PxMaterial* PxMat = OverrideMaterial->GetOrCreatePxMaterial(Physics))
			{
				return PxMat;
			}
		}
	}

	if (DefaultPhysicalMaterial)
	{
		if (PxMaterial* PxMat = DefaultPhysicalMaterial->GetOrCreatePxMaterial(Physics))
		{
			return PxMat;
		}
	}

	return DefaultMaterial;
}

static PxTransform BuildComponentLocalPose(UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp)
{
	PxTransform LocalPose(PxIdentity);
	if (!Comp || Comp == RootComp || !RootComp)
	{
		return LocalPose;
	}

	FVector RootPos = RootComp->GetWorldLocation();
	FQuat RootRot = RootComp->GetWorldMatrix().ToQuat();
	FVector CompPos = Comp->GetWorldLocation();
	FQuat CompRot = Comp->GetWorldMatrix().ToQuat();

	FQuat InvRootRot = RootRot.Inverse();
	FVector LocalPos = InvRootRot.RotateVector(CompPos - RootPos);
	FQuat LocalRot = InvRootRot * CompRot;

	return FPhysXHelper::ToPxTransform(LocalPos, LocalRot);
}

static bool ShouldCreateTriggerShape(UPrimitiveComponent* Comp)
{
	if (!Comp)
	{
		return false;
	}

	// Trigger flag 결정:
	//   1) GenerateOverlapEvents=true (명시적 trigger 의도)  OR
	//   2) 어떤 active 채널에도 Block 응답이 없음 (= simulation 의미 없음, overlap 이벤트만 의도)
	//
	// (2)가 핵심 - FilterShader의 PairFlag만으로는 simulation shape pair에서 contact resolve를
	// 막지 못하는 경우가 있어, 응답이 모두 Overlap/Ignore이면 PhysX shape 자체를 trigger로
	// 등록해 contact resolve 자체가 발생하지 않도록 한다.
	//
	// 같은 PxActor 안에 simulation shape와 trigger shape가 섞이면 PhysX가 거부하므로
	// 같은 액터의 모든 컴포넌트가 같은 종류여야 안전 (현재 ATriggerVolumeBase는 BoxComponent 1개라 OK).
	if (Comp->GetGenerateOverlapEvents())
	{
		return true;
	}

	for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
	{
		if (Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Ch)) == ECollisionResponse::Block)
		{
			return false;
		}
	}
	return true;
}

static void ConfigureCreatedShape(PxShape* Shape, UPrimitiveComponent* Comp, FBodyInstance* BodyInstance, bool bShouldBeTrigger)
{
	if (!Shape || !Comp)
	{
		return;
	}

	FPhysXCollision::SetupFilterData(Shape, Comp);

	if (bShouldBeTrigger)
	{
		Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
		Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
	}

	// userData: shape도 FBodyInstance로 매핑한다.
	FPhysXHelper::SetUserData(Shape, BodyInstance);
}

PxShape* FPhysXPhysicsScene::AddShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp)
{
	if (!Mapping.Actor || !DefaultMaterial || !Comp) return nullptr;

	const PxTransform ComponentLocalPose = BuildComponentLocalPose(Mapping.RootComp, Comp);
	const bool bShouldBeTrigger = ShouldCreateTriggerShape(Comp);

	// Shape Component 타입에 따라 PxGeometry 결정
	PxGeometryHolder Geom;
	bool bHasGeom = false;

	// Capsule은 PhysX에서 X축 기준이므로 로컬 회전 보정 필요
	PxQuat ShapeAxisRot = PxQuat(PxIdentity);

	if (auto* Box = Cast<UBoxComponent>(Comp))
	{
		FVector Ext = Box->GetScaledBoxExtent();
		Geom = PxBoxGeometry(Ext.X, Ext.Y, Ext.Z);
		bHasGeom = true;
	}
	else if (auto* Sphere = Cast<USphereComponent>(Comp))
	{
		Geom = PxSphereGeometry(Sphere->GetScaledSphereRadius());
		bHasGeom = true;
	}
	else if (auto* Capsule = Cast<UCapsuleComponent>(Comp))
	{
		float Radius = Capsule->GetScaledCapsuleRadius();
		float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		Geom = PxCapsuleGeometry(Radius, HalfHeight - Radius);
		ShapeAxisRot = PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f));
		bHasGeom = true;
	}

	if (!bHasGeom) return nullptr;

	PxMaterial* ShapeMaterial = TryGetOrCreatePxMaterial(Comp, DefaultPhysicalMaterial, DefaultMaterial, Physics);
	if (!ShapeMaterial)
	{
		UE_LOG("[PhysX] Failed to resolve material for component. Comp=%p", Comp);
		return nullptr;
	}

	PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Mapping.Actor, Geom.any(), *ShapeMaterial);
	if (!Shape) return nullptr;

	// Capsule 등 축 보정을 LocalPose의 회전 부분에 합성
	PxTransform LocalPose = ComponentLocalPose;
	LocalPose.q = LocalPose.q * ShapeAxisRot;
	Shape->setLocalPose(LocalPose);

	ConfigureCreatedShape(Shape, Comp, Comp->GetBodyInstance(), bShouldBeTrigger);

	return Shape;
}

// PxShape::userData는 해당 UPrimitiveComponent가 소유한 FBodyInstance*이다.
// 같은 PxActor에 여러 component shape가 붙어도 body instance 포인터로 component 단위 detach가 가능하다.
void FPhysXPhysicsScene::DetachShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp)
{
	// TODO(Physics): PxShape::userData는 FBodyInstance*이다.
	// 따라서 Component 단위 detach를 하려면 Shape -> Component 역매핑이 필요하다.
	// BodyMapping 제거 / BodySetup 기반 생성 경로로 전환할 때 함께 정리한다.
	// 현재는 Actor 전체 release 경로를 기준으로 동작시킨다.
	if (!Mapping.Actor || !Comp) return;
	FBodyInstance* ComponentBody = Comp->GetBodyInstance();
	if (!ComponentBody) return;

	const PxU32 NumShapes = Mapping.Actor->getNbShapes();
	if (NumShapes == 0) return;

	std::vector<PxShape*> Shapes(NumShapes);
	Mapping.Actor->getShapes(Shapes.data(), NumShapes);

	for (PxShape* Shape : Shapes)
	{
		if (Shape && FPhysXHelper::HasUserData(Shape, ComponentBody))
		{
			Mapping.Actor->detachShape(*Shape);
		}
	}
}

FPhysXPhysicsScene::FBodyMapping* FPhysXPhysicsScene::FindMappingByActor(AActor* OwnerActor)
{
	for (auto& M : BodyMappings)
	{
		if (M && M->OwnerActor == OwnerActor) return M.get();
	}
	return nullptr;
}

const FPhysXPhysicsScene::FBodyMapping* FPhysXPhysicsScene::FindMappingByActor(AActor* OwnerActor) const
{
	for (const auto& M : BodyMappings)
	{
		if (M && M->OwnerActor == OwnerActor) return M.get();
	}
	return nullptr;
}

// "이 컴포넌트가 shape로 추가된 mapping" 검색 - 등록 가드 + Force/Velocity API 라우팅용.
// owner 기반 lookup과 다름: 같은 owner라도 컴포넌트가 아직 Components에 push되지 않았으면
// 다른 컴포넌트의 shape를 통해 force가 잘못 적용되지 않도록 nullptr 반환.
FPhysXPhysicsScene::FBodyMapping* FPhysXPhysicsScene::FindMappingByComponent(UPrimitiveComponent* Comp)
{
	if (!Comp) return nullptr;

	for (auto& M : BodyMappings)
	{
		if (!M) continue;

		for (UPrimitiveComponent* C : M->Components)
		{
			if (C == Comp)
			{
				return M.get();
			}
		}
	}
	return nullptr;
}

const FPhysXPhysicsScene::FBodyMapping* FPhysXPhysicsScene::FindMappingByComponent(UPrimitiveComponent* Comp) const
{
	if (!Comp) return nullptr;

	for (const auto& M : BodyMappings)
	{
		if (!M) continue;

		for (UPrimitiveComponent* C : M->Components)
		{
			if (C == Comp)
			{
				return M.get();
			}
		}
	}
	return nullptr;
}

// ============================================================
// Force / Torque
// ============================================================

void FPhysXPhysicsScene::AddForce(UPrimitiveComponent* Comp, const FVector& Force)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->AddForce(Force);
}

void FPhysXPhysicsScene::AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->AddForceAtLocation(Force, WorldLocation);
}

void FPhysXPhysicsScene::AddTorque(UPrimitiveComponent* Comp, const FVector& Torque)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->AddTorque(Torque);
}

// ============================================================
// Velocity
// ============================================================

FVector FPhysXPhysicsScene::GetLinearVelocity(UPrimitiveComponent* Comp) const
{
	const FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return FVector(0, 0, 0);
	return BodyInstance->GetLinearVelocity();
}

void FPhysXPhysicsScene::SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->SetLinearVelocity(Vel);
}

FVector FPhysXPhysicsScene::GetAngularVelocity(UPrimitiveComponent* Comp) const
{
	const FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return FVector(0, 0, 0);
	return BodyInstance->GetAngularVelocity();
}

void FPhysXPhysicsScene::SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->SetAngularVelocity(Vel);
}

// ============================================================
// Mass
// ============================================================

void FPhysXPhysicsScene::SetMass(UPrimitiveComponent* Comp, float NewMass)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->SetBodyMass(NewMass);
}

float FPhysXPhysicsScene::GetMass(UPrimitiveComponent* Comp) const
{
	const FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return 1.f;
	return BodyInstance->GetBodyMass();
}

void FPhysXPhysicsScene::SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->SetCenterOfMassLocal(LocalOffset);
}

FVector FPhysXPhysicsScene::GetCenterOfMass(UPrimitiveComponent* Comp) const
{
	const FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return FVector(0.f, 0.f, 0.f);
	return BodyInstance->GetCenterOfMassLocal();
}

// ============================================================
// Body Instance
// ============================================================

FBodyInstance* FPhysXPhysicsScene::GetBodyInstance(UPrimitiveComponent* Comp)
{
	if (!Comp || !FindMappingByComponent(Comp))
	{
		return nullptr;
	}

	FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	return BodyInstance && BodyInstance->IsValidBodyInstance() ? BodyInstance : nullptr;
}

const FBodyInstance* FPhysXPhysicsScene::GetBodyInstance(UPrimitiveComponent* Comp) const
{
	if (!Comp || !FindMappingByComponent(Comp))
	{
		return nullptr;
	}

	const FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	return BodyInstance && BodyInstance->IsValidBodyInstance() ? BodyInstance : nullptr;
}
