#include "PhysXPhysicsScene.h"

#include "Component/PrimitiveComponent.h"
#include "Core/Logging/Log.h"
#include "Physics/PhysicsMaterial/PhysicalMaterial.h"
#include "PhysXCollision.h"
#include "PhysXHelper.h"
#include "PhysXShapeDesc.h"

#include <PxPhysicsAPI.h>

#include <vector>

using namespace physx;

static PxMaterial* TryGetOrCreatePxMaterial(const FPhysXShapeMaterialDesc& Material, UPhysicalMaterial* DefaultPhysicalMaterial, PxMaterial* DefaultMaterial, PxPhysics* Physics)
{
	if (!Physics) return DefaultMaterial;

	if (Material.OverrideMaterial)
	{
		if (PxMaterial* PxMat = Material.OverrideMaterial->GetOrCreatePxMaterial(Physics))
		{
			return PxMat;
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

static bool ShouldCreateTriggerShape(const FPhysXShapeCollisionDesc& Collision)
{
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
	if (Collision.bGenerateOverlapEvents)
	{
		return true;
	}

	for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
	{
		if (Collision.Responses.GetResponse(static_cast<ECollisionChannel>(Ch)) == ECollisionResponse::Block)
		{
			return false;
		}
	}
	return true;
}

static EPhysXBodyType GetBodyType(const PxRigidActor* Actor)
{
	if (const PxRigidDynamic* Dynamic = Actor ? Actor->is<PxRigidDynamic>() : nullptr)
	{
		return Dynamic->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC)
			? EPhysXBodyType::Kinematic
			: EPhysXBodyType::Dynamic;
	}

	return EPhysXBodyType::Static;
}

static bool BuildPxGeometry(const FPhysXShapeDesc& Desc, PxGeometryHolder& OutGeometry)
{
	switch (Desc.ShapeType)
	{
	case EPhysXShapeType::Box:
		OutGeometry = PxBoxGeometry(Desc.BoxHalfExtent.X, Desc.BoxHalfExtent.Y, Desc.BoxHalfExtent.Z);
		return true;
	case EPhysXShapeType::Sphere:
		OutGeometry = PxSphereGeometry(Desc.Radius);
		return true;
	case EPhysXShapeType::Capsule:
		OutGeometry = PxCapsuleGeometry(Desc.Radius, Desc.HalfHeight - Desc.Radius);
		return true;
	default:
		return false;
	}
}

static void ConfigureCreatedShape(PxShape* Shape, const FPhysXShapeDesc& Desc)
{
	if (!Shape)
	{
		return;
	}

	FPhysXCollision::SetupFilterData(Shape, Desc.Collision);

	if (ShouldCreateTriggerShape(Desc.Collision))
	{
		Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
		// scene query 자동 제외. 이 flag를 끄지 않으면 trigger shape도 raycast/sweep에
		// 그대로 잡힌다(PxShape.h: trigger는 eSCENE_QUERY_SHAPE가 켜져 있으면 query에 참여).
		// 특히 RaycastByObjectTypes는 ObjectType만 보고 응답을 보지 않아 trigger가 hit으로 새어 나온다.
		// trigger overlap은 simulation callback으로 처리하므로 query 제외해도 이벤트는 유지된다.
		Shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
		Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
	}

	FPhysXHelper::SetShapeBodyRecord(Shape, Desc.BodyInstance);
}

// FPhysXShapeDesc 하나를 주어진 actor에 PxShape로 생성한다.
// ShapeComponent / BodySetup / Ragdoll 경로가 같은 shape 생성 절차를 공유한다.
PxShape* FPhysXPhysicsScene::CreateShapeOnActor(PxRigidActor* Actor, const FPhysXShapeDesc& Desc)
{
	if (!Actor || !DefaultMaterial) return nullptr;

	PxGeometryHolder Geom;
	if (!BuildPxGeometry(Desc, Geom)) return nullptr;

	PxMaterial* ShapeMaterial = TryGetOrCreatePxMaterial(Desc.Material, DefaultPhysicalMaterial, DefaultMaterial, Physics);
	if (!ShapeMaterial) return nullptr;

	PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Actor, Geom.any(), *ShapeMaterial);
	if (!Shape) return nullptr;

	Shape->setLocalPose(FPhysXHelper::ToPxTransform(Desc.LocalTransform));
	ConfigureCreatedShape(Shape, Desc);

	return Shape;
}

PxShape* FPhysXPhysicsScene::AddShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp)
{
	if (!Mapping.Actor || !DefaultMaterial || !Comp) return nullptr;

	FPhysXShapeDesc Desc;
	if (!FPhysXShapeDescUtils::MakeShapeDescFromShapeComponent(Mapping.RootComp, Comp, GetBodyType(Mapping.Actor), Desc)) return nullptr;

	return CreateShapeOnActor(Mapping.Actor, Desc);
}

bool FPhysXPhysicsScene::AddShapesFromBodySetup(FBodyMapping& Mapping, UPrimitiveComponent* Comp)
{
	if (!Mapping.Actor || !DefaultMaterial || !Comp) return false;

	TArray<FPhysXShapeDesc> Descs;
	FPhysXShapeDescUtils::MakeShapeDescsFromBodySetup(Mapping.RootComp, Comp, GetBodyType(Mapping.Actor), Descs);
	if (Descs.empty()) return false;

	bool bAnyCreated = false;
	for (const FPhysXShapeDesc& Desc : Descs)
	{
		if (CreateShapeOnActor(Mapping.Actor, Desc) != nullptr)
		{
			bAnyCreated = true;
		}
	}
	return bAnyCreated;
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
		if (Shape && FPhysXHelper::IsShapeBodyRecord(Shape, ComponentBody))
		{
			FPhysXHelper::SetShapeBodyRecord(Shape, nullptr);
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
