#pragma once

#include "Core/Types/CollisionTypes.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Math/Vector.h"

class FBodyInstance;
class UPrimitiveComponent;
class UPhysicalMaterial;

// PhysX body 생성 방식 분류.
enum class EPhysXBodyType : uint8
{
	Static,
	Dynamic,
	Kinematic
};

// PhysX에 넘길 수 있는 기본 shape 종류.
enum class EPhysXShapeType : uint8
{
	Box,
	Sphere,
	Capsule
};

// Shape filter data를 만들기 위한 충돌 설정 snapshot.
struct FPhysXShapeCollisionDesc
{
	ECollisionEnabled CollisionEnabled = ECollisionEnabled::NoCollision;
	ECollisionChannel ObjectType = ECollisionChannel::WorldStatic;
	FCollisionResponseContainer Responses;
	uint32 OwnerActorId = 0;
	bool bGenerateOverlapEvents = false;
};

// Shape material 선택에 필요한 물리 재질 snapshot.
struct FPhysXShapeMaterialDesc
{
	UPhysicalMaterial* OverrideMaterial = nullptr;
};

// PhysX shape 생성에 필요한 최소 런타임 입력값.
struct FPhysXShapeDesc
{
	EPhysXBodyType BodyType = EPhysXBodyType::Static;
	EPhysXShapeType ShapeType = EPhysXShapeType::Box;
	FTransform LocalTransform;

	FVector BoxHalfExtent = FVector(0.5f, 0.5f, 0.5f);
	float Radius = 0.0f;
	float HalfHeight = 0.0f;

	FPhysXShapeCollisionDesc Collision;
	FPhysXShapeMaterialDesc Material;
	FBodyInstance* BodyInstance = nullptr;
};

namespace FPhysXShapeDescUtils
{
	bool MakeShapeDescFromShapeComponent(
		UPrimitiveComponent* RootComp,
		UPrimitiveComponent* ShapeComp,
		EPhysXBodyType BodyType,
		FPhysXShapeDesc& OutDesc);
}
