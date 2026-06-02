#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Object/Object.h"

class UClothComponent;

UENUM()
enum class EClothCollisionMode : uint8
{
	None,
	WorldShapes
};

struct FClothCollisionData
{
	TArray<FVector4> Spheres;
	TArray<uint32> Capsules;
	TArray<FVector4> Planes;
	TArray<uint32> ConvexMasks;

	void Reset();
	uint32 GetPrimitiveCount() const;
};

struct FClothCollisionGatherDesc
{
	const UClothComponent* ClothComponent = nullptr;
	float CollisionThickness = 0.0f;
};
