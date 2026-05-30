#include "Physics/PhysXShapeDesc.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "GameFramework/AActor.h"
#include "Math/MathUtils.h"

static FTransform BuildComponentLocalTransform(UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp)
{
	if (!Comp || Comp == RootComp || !RootComp)
	{
		return FTransform();
	}

	FVector RootPos = RootComp->GetWorldLocation();
	FQuat RootRot = RootComp->GetWorldMatrix().ToQuat();
	FVector CompPos = Comp->GetWorldLocation();
	FQuat CompRot = Comp->GetWorldMatrix().ToQuat();

	FQuat InvRootRot = RootRot.Inverse();
	FVector LocalPos = InvRootRot.RotateVector(CompPos - RootPos);
	FQuat LocalRot = InvRootRot * CompRot;

	return FTransform(LocalPos, LocalRot, FVector::OneVector);
}

static FPhysXShapeCollisionDesc BuildCollisionDesc(UPrimitiveComponent* Comp)
{
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

bool FPhysXShapeDescUtils::MakeShapeDescFromShapeComponent(
	UPrimitiveComponent* RootComp,
	UPrimitiveComponent* ShapeComp,
	EPhysXBodyType BodyType,
	FPhysXShapeDesc& OutDesc)
{
	if (!ShapeComp) return false;

	OutDesc = FPhysXShapeDesc();
	OutDesc.BodyType = BodyType;
	OutDesc.LocalTransform = BuildComponentLocalTransform(RootComp, ShapeComp);
	OutDesc.Collision = BuildCollisionDesc(ShapeComp);
	OutDesc.Material.OverrideMaterial = ShapeComp->GetPhysicalMaterialOverride();
	OutDesc.BodyInstance = ShapeComp->GetBodyInstance();

	if (auto* Box = Cast<UBoxComponent>(ShapeComp))
	{
		OutDesc.ShapeType = EPhysXShapeType::Box;
		OutDesc.BoxHalfExtent = Box->GetScaledBoxExtent();
		return true;
	}

	if (auto* Sphere = Cast<USphereComponent>(ShapeComp))
	{
		OutDesc.ShapeType = EPhysXShapeType::Sphere;
		OutDesc.Radius = Sphere->GetScaledSphereRadius();
		return true;
	}

	if (auto* Capsule = Cast<UCapsuleComponent>(ShapeComp))
	{
		OutDesc.ShapeType = EPhysXShapeType::Capsule;
		OutDesc.Radius = Capsule->GetScaledCapsuleRadius();
		OutDesc.HalfHeight = Capsule->GetScaledCapsuleHalfHeight();

		// Capsule은 PhysX에서 X축 기준이므로 로컬 회전 보정 필요.
		OutDesc.LocalTransform.Rotation *= FQuat::FromAxisAngle(FVector(0.0f, 0.0f, 1.0f), FMath::Pi * 0.5f);
		return true;
	}

	return false;
}
