#include "PhysXPhysicsScene.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "Mesh/Static/StaticMesh.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsMaterial/PhysicalMaterial.h"
#include "PhysXCollision.h"
#include "PhysXHelper.h"
#include "PhysXShapeDesc.h"

#include <PxPhysicsAPI.h>

#include <cfloat>
#include <cmath>
#include <vector>

using namespace physx;

namespace
{
	int32 CountValidRagdollBodies(USkeletalMeshComponent* Comp)
	{
		if (!Comp || !Comp->IsRagdollSimulating())
		{
			return 0;
		}

		int32 Count = 0;
		for (const std::unique_ptr<FBodyInstance>& Body : Comp->GetBodies())
		{
			if (Body && Body->IsValidBodyInstance() && Body->IsSimulatingPhysics())
			{
				++Count;
			}
		}
		return Count;
	}

	FTransform BuildComponentLocalTransformForTriangleMesh(UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp)
	{
		// primitive shape 경로의 FPhysXShapeDesc와 같은 기준: 모든 shape local pose는 대표 RootComp 기준이다.
		// scale은 pose에 넣지 않고 PxTriangleMeshGeometry의 PxMeshScale로 따로 넘겨야 PhysX mesh scale 제약을 통과한다.
		if (!Comp || Comp == RootComp || !RootComp)
		{
			return FTransform();
		}

		const FVector RootPos = RootComp->GetWorldLocation();
		const FQuat RootRot = RootComp->GetWorldMatrix().ToQuat();
		const FVector CompPos = Comp->GetWorldLocation();
		const FQuat CompRot = Comp->GetWorldMatrix().ToQuat();

		const FQuat InvRootRot = RootRot.Inverse();
		const FVector LocalPos = InvRootRot.RotateVector(CompPos - RootPos);
		const FQuat LocalRot = InvRootRot * CompRot;

		return FTransform(LocalPos, LocalRot, FVector::OneVector);
	}

	FPhysXShapeCollisionDesc BuildTriangleMeshCollisionDesc(UPrimitiveComponent* Comp)
	{
		// triangle mesh path는 FPhysXShapeDescUtils를 거치지 않으므로 collision/filter에 필요한 값만
		// 같은 형태의 FPhysXShapeCollisionDesc로 직접 스냅샷한다.
		FPhysXShapeCollisionDesc Collision;
		if (!Comp)
		{
			return Collision;
		}

		Collision.CollisionEnabled = Comp->GetCollisionEnabled();
		Collision.ObjectType = Comp->GetCollisionObjectType();
		Collision.Responses = Comp->GetCollisionResponseContainer();
		Collision.OwnerActorId = Comp->GetOwner() ? Comp->GetOwner()->GetUUID() : 0;
		Collision.bGenerateOverlapEvents = Comp->GetGenerateOverlapEvents();
		return Collision;
	}

	float SanitizeTriangleMeshScaleComponent(float Value)
	{
		// PhysX PxMeshScale은 0에 가까운 축 scale을 invalid geometry로 본다.
		// StaticMeshComponent scale이 0이거나 비정상 값이면 shape 생성 실패 대신 최소 유효 scale로 보정한다.
		if (!std::isfinite(Value))
		{
			return 1.0f;
		}

		if (std::fabs(Value) >= PX_MESH_SCALE_MIN)
		{
			return Value;
		}

		// 음수 scale은 좌표계 반전을 표현할 수 있으므로 부호는 유지하고 절댓값만 PhysX 최소값 이상으로 올린다.
		return Value < 0.0f ? -PX_MESH_SCALE_MIN : PX_MESH_SCALE_MIN;
	}

	template<typename ShapeSetupFunc>
	PxShape* AttachTriangleMeshShape(
		PxPhysics* Physics,
		PxRigidActor* RigidActor,
		PxMaterial* Material,
		const TArray<uint8>& CookedData,
		const FVector& Scale,
		const PxTransform& BaseLocalPose,
		ShapeSetupFunc SetupShape)
	{
		// CookedData -> PxTriangleMesh -> PxTriangleMeshGeometry -> PxShape 순서로 PhysX 객체를 만든다.
		// cooked byte는 플랫폼/PhysX version에 맞게 cook된 binary이고, 여기서 createTriangleMesh()가
		// simulation/query에 필요한 runtime mesh 객체로 복원한다.
		if (!Physics || !RigidActor || !Material || CookedData.empty())
		{
			return nullptr;
		}

		PxDefaultMemoryInputData InputData(const_cast<PxU8*>(CookedData.data()), static_cast<PxU32>(CookedData.size()));
		PxTriangleMesh* TriangleMesh = Physics->createTriangleMesh(InputData);
		if (!TriangleMesh)
		{
			UE_LOG("[PhysX] Failed to create PxTriangleMesh from cooked data.");
			return nullptr;
		}

		// StaticMesh asset의 vertex는 asset local space에 있고, component world scale은 shape geometry scale로 적용한다.
		// 이렇게 해야 같은 cooked mesh byte를 scale만 달리하여 여러 StaticMeshComponent에서 재사용할 수 있다.
		const PxVec3 MeshScale(
			SanitizeTriangleMeshScaleComponent(Scale.X),
			SanitizeTriangleMeshScaleComponent(Scale.Y),
			SanitizeTriangleMeshScaleComponent(Scale.Z));

		PxTriangleMeshGeometry TriGeom(TriangleMesh, PxMeshScale(MeshScale));
		if (!TriGeom.isValid())
		{
			TriangleMesh->release();
			UE_LOG("[PhysX] Invalid PxTriangleMeshGeometry (Scale=%f,%f,%f).", Scale.X, Scale.Y, Scale.Z);
			return nullptr;
		}

		// exclusive shape로 만들면 PxRigidActor가 shape를 소유한다.
		// 이후 component unregister/detach 시 actor에서 shape를 떼며 PhysX가 참조를 정리한다.
		PxShape* Shape = PxRigidActorExt::createExclusiveShape(*RigidActor, TriGeom, *Material);
		// createExclusiveShape가 성공하면 PxShape가 PxTriangleMesh reference를 잡는다.
		// 실패한 경우에도 local reference만 남아 있으므로 여기서 release한다.
		TriangleMesh->release();
		if (!Shape)
		{
			UE_LOG("[PhysX] Failed to create triangle mesh PxShape.");
			return nullptr;
		}

		Shape->setLocalPose(BaseLocalPose);
		// filter data/userData처럼 shape 생성 후 붙여야 하는 엔진 메타데이터는 호출자가 주입한다.
		SetupShape(Shape);
		return Shape;
	}
}

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
	constexpr float MinShapeExtent = 1.0e-4f;
	auto SanitizeExtent = [](float Value) -> float
		{
			if (!std::isfinite(Value))
			{
				return 0.0f;
			}
			return std::fabs(Value);
		};

	switch (Desc.ShapeType)
	{
	case EPhysXShapeType::Box:
	{
		const float X = SanitizeExtent(Desc.BoxHalfExtent.X);
		const float Y = SanitizeExtent(Desc.BoxHalfExtent.Y);
		const float Z = SanitizeExtent(Desc.BoxHalfExtent.Z);
		if (X < MinShapeExtent || Y < MinShapeExtent || Z < MinShapeExtent)
		{
			UE_LOG("[PhysX] Invalid box shape skipped. HalfExtent=(%f,%f,%f)",
				Desc.BoxHalfExtent.X,
				Desc.BoxHalfExtent.Y,
				Desc.BoxHalfExtent.Z);
			return false;
		}
		OutGeometry = PxBoxGeometry(X, Y, Z);
		return true;
	}
	case EPhysXShapeType::Sphere:
	{
		const float Radius = SanitizeExtent(Desc.Radius);
		if (Radius < MinShapeExtent)
		{
			UE_LOG("[PhysX] Invalid sphere shape skipped. Radius=%f", Desc.Radius);
			return false;
		}
		OutGeometry = PxSphereGeometry(Radius);
		return true;
	}
	case EPhysXShapeType::Capsule:
	{
		const float Radius = SanitizeExtent(Desc.Radius);
		const float HalfHeight = SanitizeExtent(Desc.HalfHeight);
		const float HalfCylinderHeight = HalfHeight - Radius;
		if (Radius < MinShapeExtent || HalfCylinderHeight < MinShapeExtent)
		{
			UE_LOG("[PhysX] Invalid capsule shape skipped. Radius=%f HalfHeight=%f",
				Desc.Radius,
				Desc.HalfHeight);
			return false;
		}
		OutGeometry = PxCapsuleGeometry(Radius, HalfCylinderHeight);
		return true;
	}
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

bool FPhysXPhysicsScene::AddTriangleMeshShapeFromStaticMesh(PxRigidActor* HostActor, UPrimitiveComponent* RootComp, UStaticMeshComponent* Comp)
{
	// StaticMeshComponent가 BodySetup을 가진 경우, AggGeom primitive 대신 package에 저장된 cooked triangle collision을 먼저 시도한다.
	// 런타임 등록 단계에서는 vertex/index를 다시 훑거나 recook하지 않는다.
	if (!HostActor || !RootComp || !Comp || !Physics || !DefaultMaterial)
	{
		return false;
	}

	// PhysX triangle mesh simulation shape는 PxRigidStatic 전용으로 사용한다.
	// 움직이는 StaticMesh는 convex/simple collision이 필요하므로 여기서는 등록하지 않는다.
	// false를 반환하면 호출자는 AggGeom fallback을 시도할 수 있다.
	if (GetBodyType(HostActor) != EPhysXBodyType::Static)
	{
		UE_LOG("[PhysX] StaticMesh triangle collision skipped: PxTriangleMesh simulation shape requires PxRigidStatic.");
		return false;
	}

	UStaticMesh* StaticMesh = Comp->GetStaticMesh();
	if (!StaticMesh || !StaticMesh->IsTriangleMeshCollisionEnabled())
	{
		return false;
	}

	UBodySetup* BodySetup = StaticMesh ? StaticMesh->GetBodySetup() : nullptr;
	if (!BodySetup)
	{
		return false;
	}

	if (!BodySetup->IsCookedTriangleMeshPhysXDataCompatible(
		static_cast<int32>(PX_PHYSICS_VERSION),
		UBodySetup::StaticMeshTriangleCollisionCookingVersion))
	{
		UE_LOG("[PhysX] StaticMesh triangle collision skipped: cooked data is missing or incompatible. PhysX=%d CookVersion=%d",
			BodySetup->GetTriangleMeshPhysXVersion(),
			BodySetup->GetTriangleMeshCookingVersion());
		return false;
	}

	FPhysXShapeMaterialDesc MaterialDesc;
	MaterialDesc.OverrideMaterial = Comp->GetPhysicalMaterialOverride();
	PxMaterial* ShapeMaterial = TryGetOrCreatePxMaterial(MaterialDesc, DefaultPhysicalMaterial, DefaultMaterial, Physics);
	if (!ShapeMaterial)
	{
		return false;
	}

	// local pose는 대표 RootComp 기준 위치/회전만 담고, component scale은 PxMeshScale로 따로 적용한다.
	// 이렇게 해야 compound actor 안에 여러 component shape가 있어도 primitive path와 같은 좌표 기준을 유지한다.
	const FTransform LocalTransform = BuildComponentLocalTransformForTriangleMesh(RootComp, Comp);
	const PxTransform LocalPose = FPhysXHelper::ToPxTransform(LocalTransform);
	const FPhysXShapeCollisionDesc Collision = BuildTriangleMeshCollisionDesc(Comp);

	// Triangle mesh trigger shape는 PhysX에서 지원하지 않는다.
	// 기존 primitive path의 ConfigureCreatedShape()는 trigger 변환을 수행하므로, 여기서는 filter/userData만 적용한다.
	// overlap-only StaticMesh를 trigger로 쓰고 싶다면 triangle mesh가 아니라 simple collision shape가 필요하다.
	if (ShouldCreateTriggerShape(Collision))
	{
		UE_LOG("[PhysX] StaticMesh triangle collision cannot become a trigger shape; registering as triangle simulation/query shape.");
	}

	PxShape* Shape = AttachTriangleMeshShape(
		Physics,
		HostActor,
		ShapeMaterial,
		BodySetup->GetCookedTriangleMeshPhysXData(),
		Comp->GetWorldScale(),
		LocalPose,
		[&Collision, Comp](PxShape* CreatedShape)
		{
			// triangle mesh path는 ConfigureCreatedShape()를 거치지 않으므로 filter data와 userData를 직접 넣는다.
			// userData에는 component의 BodyInstance를 기록해 raycast 결과 매핑과 detach가 component 단위로 동작하게 한다.
			FPhysXCollision::SetupFilterData(CreatedShape, Collision);
			FPhysXHelper::SetShapeBodyRecord(CreatedShape, Comp->GetBodyInstance());
		});

	return Shape != nullptr;
}

PxShape* FPhysXPhysicsScene::AddShapeForComponent(PxRigidActor* HostActor, UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp)
{
	if (!HostActor || !DefaultMaterial || !Comp) return nullptr;

	FPhysXShapeDesc Desc;
	if (!FPhysXShapeDescUtils::MakeShapeDescFromShapeComponent(RootComp, Comp, GetBodyType(HostActor), Desc)) return nullptr;

	return CreateShapeOnActor(HostActor, Desc);
}

bool FPhysXPhysicsScene::AddShapesFromBodySetup(PxRigidActor* HostActor, UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp)
{
	if (!HostActor || !DefaultMaterial || !Comp) return false;

	// StaticMesh는 AggGeom 대신 asset package에 저장된 cooked PxTriangleMesh를 사용한다.
	// MakeShapeDescFromShapeComponent()는 Box/Sphere/Capsule 같은 analytic geometry만 표현하므로,
	// triangle mesh는 BodySetup 처리 단계에서 StaticMeshComponent를 감지해 별도 path로 붙인다.
	// 실패 시 아래 simple shape 경로를 한 번 더 시도해 에디터에서 만든 Box/Sphere/Capsule fallback은 유지한다.
	if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Comp))
	{
		if (AddTriangleMeshShapeFromStaticMesh(HostActor, RootComp, StaticMeshComp))
		{
			return true;
		}
	}

	TArray<FPhysXShapeDesc> Descs;
	FPhysXShapeDescUtils::MakeShapeDescsFromBodySetup(RootComp, Comp, GetBodyType(HostActor), Descs);
	if (Descs.empty()) return false;

	bool bAnyCreated = false;
	for (const FPhysXShapeDesc& Desc : Descs)
	{
		if (CreateShapeOnActor(HostActor, Desc) != nullptr)
		{
			bAnyCreated = true;
		}
	}
	return bAnyCreated;
}

// PxShape::userData는 해당 UPrimitiveComponent가 소유한 FBodyInstance*이다.
// 같은 PxActor에 여러 component shape가 붙어도 그 포인터로 component 단위 detach가 가능하다.
void FPhysXPhysicsScene::DetachShapeForComponent(PxRigidActor* HostActor, UPrimitiveComponent* Comp)
{
	if (!HostActor || !Comp) return;
	FBodyInstance* ComponentBody = Comp->GetBodyInstance();
	if (!ComponentBody) return;

	const PxU32 NumShapes = HostActor->getNbShapes();
	if (NumShapes == 0) return;

	std::vector<PxShape*> Shapes(NumShapes);
	HostActor->getShapes(Shapes.data(), NumShapes);

	for (PxShape* Shape : Shapes)
	{
		if (Shape && FPhysXHelper::IsShapeBodyRecord(Shape, ComponentBody))
		{
			FPhysXHelper::SetShapeBodyRecord(Shape, nullptr);
			HostActor->detachShape(*Shape);
		}
	}
}

FBodyInstance* FPhysXPhysicsScene::FindHostBodyByActor(AActor* OwnerActor)
{
	if (!OwnerActor) return nullptr;
	for (FBodyInstance* Host : Bodies)
	{
		if (Host && Host->GetOwnerComponent() && Host->GetOwnerActor() == OwnerActor)
		{
			return Host;
		}
	}
	return nullptr;
}

// Comp가 속한 강체의 대표 body를 찾는다 - 등록 여부 확인 + Force/Velocity API 라우팅용.
// 액터가 같아도 Comp가 아직 그 강체에 등록되기 전이면, 엉뚱한 컴포넌트에 힘이 가지 않도록 nullptr 반환.
FBodyInstance* FPhysXPhysicsScene::FindHostBody(UPrimitiveComponent* Comp)
{
	if (!Comp) return nullptr;

	for (FBodyInstance* Host : Bodies)
	{
		if (!Host) continue;
		if (Host->GetOwnerComponent() == Comp) return Host;
		for (UPrimitiveComponent* C : Host->CombinedComponents)
		{
			if (C == Comp) return Host;
		}
	}
	return nullptr;
}

const FBodyInstance* FPhysXPhysicsScene::FindHostBody(UPrimitiveComponent* Comp) const
{
	if (!Comp) return nullptr;

	for (const FBodyInstance* Host : Bodies)
	{
		if (!Host) continue;
		if (Host->GetOwnerComponent() == Comp) return Host;
		for (UPrimitiveComponent* C : Host->CombinedComponents)
		{
			if (C == Comp) return Host;
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

void FPhysXPhysicsScene::AddImpulse(UPrimitiveComponent* Comp, const FVector& Impulse)
{
	if (USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(Comp))
	{
		const int32 BodyCount = CountValidRagdollBodies(SkeletalComp);
		if (BodyCount > 0)
		{
			const FVector PerBodyImpulse = Impulse / static_cast<float>(BodyCount);
			for (std::unique_ptr<FBodyInstance>& Body : SkeletalComp->GetBodies())
			{
				if (Body && Body->IsValidBodyInstance() && Body->IsSimulatingPhysics())
				{
					Body->AddImpulse(PerBodyImpulse);
				}
			}
			return;
		}
	}

	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->AddImpulse(Impulse);
}

void FPhysXPhysicsScene::AddImpulseAtLocation(UPrimitiveComponent* Comp, const FVector& Impulse, const FVector& WorldLocation)
{
	if (USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(Comp))
	{
		const int32 BodyCount = CountValidRagdollBodies(SkeletalComp);
		if (BodyCount > 0)
		{
			const FVector PerBodyImpulse = Impulse / static_cast<float>(BodyCount);
			for (std::unique_ptr<FBodyInstance>& Body : SkeletalComp->GetBodies())
			{
				if (Body && Body->IsValidBodyInstance() && Body->IsSimulatingPhysics())
				{
					Body->AddImpulseAtLocation(PerBodyImpulse, WorldLocation);
				}
			}
			return;
		}
	}

	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->AddImpulseAtLocation(Impulse, WorldLocation);
}

void FPhysXPhysicsScene::AddAngularImpulse(UPrimitiveComponent* Comp, const FVector& AngularImpulse)
{
	if (USkeletalMeshComponent* SkeletalComp = Cast<USkeletalMeshComponent>(Comp))
	{
		const int32 BodyCount = CountValidRagdollBodies(SkeletalComp);
		if (BodyCount > 0)
		{
			const FVector PerBodyAngularImpulse = AngularImpulse / static_cast<float>(BodyCount);
			for (std::unique_ptr<FBodyInstance>& Body : SkeletalComp->GetBodies())
			{
				if (Body && Body->IsValidBodyInstance() && Body->IsSimulatingPhysics())
				{
					Body->AddAngularImpulse(PerBodyAngularImpulse);
				}
			}
			return;
		}
	}

	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->AddAngularImpulse(AngularImpulse);
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
	if (!Comp || !FindHostBody(Comp))
	{
		return nullptr;
	}

	FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	return BodyInstance && BodyInstance->IsValidBodyInstance() ? BodyInstance : nullptr;
}

const FBodyInstance* FPhysXPhysicsScene::GetBodyInstance(UPrimitiveComponent* Comp) const
{
	if (!Comp || !FindHostBody(Comp))
	{
		return nullptr;
	}

	const FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	return BodyInstance && BodyInstance->IsValidBodyInstance() ? BodyInstance : nullptr;
}

// Vehicle 등 PhysX 직접 제어용 — Comp가 속한 강체의 PxRigidActor. 미등록이면 nullptr.
physx::PxRigidActor* FPhysXPhysicsScene::GetComponentRigidActor(UPrimitiveComponent* Comp)
{
	FBodyInstance* Host = FindHostBody(Comp);
	return Host ? FPhysXHelper::GetRigidActor(Host) : nullptr;
}
