#include "PhysXShapeDesc.h"

#include "Component/PrimitiveComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "GameFramework/AActor.h"
#include "Math/MathUtils.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsGeometry.h"
#include <algorithm>
#include <cmath>

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

static FVector MulComponentWise(const FVector& A, const FVector& B)
{
	return FVector(A.X * B.X, A.Y * B.Y, A.Z * B.Z);
}

static FTransform BuildElemLocalTransform(const FTransform& CompLocalTransform, const FKShapeElem& Elem, const FVector& WorldScale)
{
	FVector ScaledElemPos = MulComponentWise(Elem.Transform.Location, WorldScale);
	FQuat CompRot = CompLocalTransform.Rotation;
	FVector FinalPos = CompLocalTransform.Location + CompRot.RotateVector(ScaledElemPos);
	FQuat FinalRot = CompRot * Elem.Transform.Rotation;
	return FTransform(FinalPos, FinalRot, FVector::OneVector);
}

static float SanitizeUniformScale(float UniformScale)
{
	if (!std::isfinite(UniformScale))
	{
		return 1.0f;
	}
	return std::max(std::fabs(UniformScale), 1.0e-4f);
}

static FTransform ScaleLocalTransformUniform(const FTransform& Transform, float UniformScale)
{
	FTransform Result = Transform;
	Result.Location *= UniformScale;
	Result.Scale = FVector::OneVector;
	return Result;
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

		// PhysX 캡슐은 로컬 X축 기준 → 엔진 Z-up(수직)으로 세우려면 Y축으로 90° 회전.
		OutDesc.LocalTransform.Rotation *= FQuat::FromAxisAngle(FVector(0.0f, 1.0f, 0.0f), FMath::Pi * 0.5f);
		return true;
	}

	return false;
}

void FPhysXShapeDescUtils::MakeShapeDescsFromBodySetup(
	UPrimitiveComponent* RootComp,
	UPrimitiveComponent* Comp,
	EPhysXBodyType BodyType,
	TArray<FPhysXShapeDesc>& OutDescs)
{
	UBodySetup* BodySetup = Comp ? Comp->GetBodySetup() : nullptr;
	if (!BodySetup) return;

	const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
	FTransform CompLocalTransform = BuildComponentLocalTransform(RootComp, Comp);
	FVector WorldScale = Comp->GetWorldScale();
	FPhysXShapeCollisionDesc Collision = BuildCollisionDesc(Comp);
	FPhysXShapeMaterialDesc Material;
	Material.OverrideMaterial = Comp->GetPhysicalMaterialOverride();
	FBodyInstance* BodyInstance = Comp->GetBodyInstance();

	for (const FKSphereElem& Sphere : AggGeom.SphereElems)
	{
		FPhysXShapeDesc Desc;
		Desc.BodyType = BodyType;
		Desc.ShapeType = EPhysXShapeType::Sphere;
		Desc.Radius = Sphere.Radius * std::max(std::max(WorldScale.X, WorldScale.Y), WorldScale.Z);
		Desc.LocalTransform = BuildElemLocalTransform(CompLocalTransform, Sphere, WorldScale);
		Desc.Collision = Collision;
		Desc.Material = Material;
		Desc.BodyInstance = BodyInstance;
		OutDescs.push_back(Desc);
	}

	for (const FKBoxElem& Box : AggGeom.BoxElems)
	{
		FPhysXShapeDesc Desc;
		Desc.BodyType = BodyType;
		Desc.ShapeType = EPhysXShapeType::Box;
		Desc.BoxHalfExtent = MulComponentWise(Box.Extent, WorldScale);
		Desc.LocalTransform = BuildElemLocalTransform(CompLocalTransform, Box, WorldScale);
		Desc.Collision = Collision;
		Desc.Material = Material;
		Desc.BodyInstance = BodyInstance;
		OutDescs.push_back(Desc);
	}

	for (const FKSphylElem& Sphyl : AggGeom.SphylElems)
	{
		float LateralScale = std::max(WorldScale.X, WorldScale.Y);
		float AxialScale = WorldScale.Z;
		float ScaledRadius = Sphyl.Radius * LateralScale;
		float ScaledHalfCylinder = Sphyl.Length * 0.5f * AxialScale;

		FPhysXShapeDesc Desc;
		Desc.BodyType = BodyType;
		Desc.ShapeType = EPhysXShapeType::Capsule;
		Desc.Radius = ScaledRadius;
		Desc.HalfHeight = ScaledHalfCylinder + ScaledRadius;
		Desc.LocalTransform = BuildElemLocalTransform(CompLocalTransform, Sphyl, WorldScale);
		// PhysX 캡슐은 로컬 X축 기준 → 엔진 Z-up(수직)으로 세우려면 Y축으로 90° 회전.
		Desc.LocalTransform.Rotation *= FQuat::FromAxisAngle(FVector(0.0f, 1.0f, 0.0f), FMath::Pi * 0.5f);
		Desc.Collision = Collision;
		Desc.Material = Material;
		Desc.BodyInstance = BodyInstance;
		OutDescs.push_back(Desc);
	}
}

void FPhysXShapeDescUtils::MakeShapeDescsFromBodySetupAsset(
	UBodySetup* BodySetup,
	EPhysXBodyType BodyType,
	const FPhysXShapeCollisionDesc& Collision,
	const FPhysXShapeMaterialDesc& Material,
	FBodyInstance* BodyInstance,
	float UniformScale,
	TArray<FPhysXShapeDesc>& OutDescs)
{
	if (!BodySetup) return;

	const float Scale = SanitizeUniformScale(UniformScale);
	const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();

	for (const FKSphereElem& Sphere : AggGeom.SphereElems)
	{
		FPhysXShapeDesc Desc;
		Desc.BodyType = BodyType;
		Desc.ShapeType = EPhysXShapeType::Sphere;
		Desc.Radius = Sphere.Radius * Scale;
		Desc.LocalTransform = ScaleLocalTransformUniform(Sphere.Transform, Scale);
		Desc.Collision = Collision;
		Desc.Material = Material;
		Desc.BodyInstance = BodyInstance;
		OutDescs.push_back(Desc);
	}

	for (const FKBoxElem& Box : AggGeom.BoxElems)
	{
		FPhysXShapeDesc Desc;
		Desc.BodyType = BodyType;
		Desc.ShapeType = EPhysXShapeType::Box;
		Desc.BoxHalfExtent = Box.Extent * Scale;
		Desc.LocalTransform = ScaleLocalTransformUniform(Box.Transform, Scale);
		Desc.Collision = Collision;
		Desc.Material = Material;
		Desc.BodyInstance = BodyInstance;
		OutDescs.push_back(Desc);
	}

	for (const FKSphylElem& Sphyl : AggGeom.SphylElems)
	{
		FPhysXShapeDesc Desc;
		Desc.BodyType = BodyType;
		Desc.ShapeType = EPhysXShapeType::Capsule;
		Desc.Radius = Sphyl.Radius * Scale;
		Desc.HalfHeight = (Sphyl.Length * 0.5f + Sphyl.Radius) * Scale;
		Desc.LocalTransform = ScaleLocalTransformUniform(Sphyl.Transform, Scale);
		// PhysX 캡슐은 로컬 X축 기준 → 엔진 Z-up(수직)으로 세우려면 Y축으로 90° 회전.
		Desc.LocalTransform.Rotation *= FQuat::FromAxisAngle(FVector(0.0f, 1.0f, 0.0f), FMath::Pi * 0.5f);
		Desc.Collision = Collision;
		Desc.Material = Material;
		Desc.BodyInstance = BodyInstance;
		OutDescs.push_back(Desc);
	}
}
