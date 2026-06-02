#include "Cloth/ClothCollisionBuilder.h"

#include <algorithm>

FVector FClothCollisionBuilder::TransformWorldPositionToClothLocal(const FMatrix& ClothWorldInverse, const FVector& WorldPosition)
{
	return ClothWorldInverse.TransformPositionWithW(WorldPosition);
}

FVector FClothCollisionBuilder::TransformWorldVectorToClothLocal(const FMatrix& ClothWorldInverse, const FVector& WorldOrigin, const FVector& WorldVector)
{
	const FVector LocalOrigin = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldOrigin);
	const FVector LocalEnd = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldOrigin + WorldVector);
	return LocalEnd - LocalOrigin;
}

float FClothCollisionBuilder::TransformWorldRadiusToClothLocal(const FMatrix& ClothWorldInverse, const FVector& WorldCenter, float WorldRadius)
{
	const FVector LocalCenter = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldCenter);
	const FVector LocalX = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldCenter + FVector(WorldRadius, 0.0f, 0.0f)) - LocalCenter;
	const FVector LocalY = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldCenter + FVector(0.0f, WorldRadius, 0.0f)) - LocalCenter;
	const FVector LocalZ = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldCenter + FVector(0.0f, 0.0f, WorldRadius)) - LocalCenter;
	return (std::max)({ LocalX.Length(), LocalY.Length(), LocalZ.Length(), 0.0f });
}

void FClothCollisionBuilder::AppendSphereFromWorldShape(const FVector& WorldCenter, float WorldRadius, float CollisionThickness, const FMatrix& ClothWorldInverse, FClothCollisionData& OutData)
{
	if (OutData.Spheres.size() >= MaxClothCollisionSpheres)
	{
		return;
	}

	const FVector LocalCenter = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldCenter);
	const float InflatedWorldRadius = (std::max)(0.0f, WorldRadius + CollisionThickness);
	const float LocalRadius = TransformWorldRadiusToClothLocal(ClothWorldInverse, WorldCenter, InflatedWorldRadius);
	if (LocalRadius <= 0.0f)
	{
		return;
	}

	OutData.Spheres.push_back(FVector4(LocalCenter, LocalRadius));
}

void FClothCollisionBuilder::AppendCapsuleFromWorldShape(
	const FVector& WorldCenter,
	const FVector& WorldAxis,
	float WorldRadius,
	float WorldSegmentHalfLength,
	float CollisionThickness,
	const FMatrix& ClothWorldInverse,
	FClothCollisionData& OutData)
{
	if (WorldSegmentHalfLength <= 1.0e-4f)
	{
		AppendSphereFromWorldShape(WorldCenter, WorldRadius, CollisionThickness, ClothWorldInverse, OutData);
		return;
	}

	if (OutData.Spheres.size() + 2 > MaxClothCollisionSpheres || OutData.Capsules.size() / 2 >= MaxClothCollisionCapsules)
	{
		return;
	}

	FVector Axis = WorldAxis;
	if (Axis.Length() <= 1.0e-4f)
	{
		Axis = FVector::ZAxisVector;
	}
	Axis.Normalize();

	const FVector WorldA = WorldCenter - Axis * WorldSegmentHalfLength;
	const FVector WorldB = WorldCenter + Axis * WorldSegmentHalfLength;
	const FVector LocalA = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldA);
	const FVector LocalB = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldB);
	const float InflatedWorldRadius = (std::max)(0.0f, WorldRadius + CollisionThickness);
	const float LocalRadius = TransformWorldRadiusToClothLocal(ClothWorldInverse, WorldCenter, InflatedWorldRadius);
	if (LocalRadius <= 0.0f)
	{
		return;
	}

	const uint32 FirstSphereIndex = static_cast<uint32>(OutData.Spheres.size());
	OutData.Spheres.push_back(FVector4(LocalA, LocalRadius));
	OutData.Spheres.push_back(FVector4(LocalB, LocalRadius));
	OutData.Capsules.push_back(FirstSphereIndex);
	OutData.Capsules.push_back(FirstSphereIndex + 1);
}

void FClothCollisionBuilder::AppendPlaneToConvex(const FVector& Normal, const FVector& Point, FClothCollisionData& OutData, uint32& ConvexMask)
{
	if (OutData.Planes.size() >= MaxClothCollisionPlanes)
	{
		return;
	}

	FVector UnitNormal = Normal;
	if (UnitNormal.Length() <= 1.0e-4f)
	{
		return;
	}
	UnitNormal.Normalize();

	const uint32 PlaneIndex = static_cast<uint32>(OutData.Planes.size());
	const float PlaneD = -UnitNormal.Dot(Point);
	OutData.Planes.push_back(FVector4(UnitNormal, PlaneD));
	ConvexMask |= (1u << PlaneIndex);
}

void FClothCollisionBuilder::AppendBoxAxisPlanes(const FVector& WorldCenter, const FVector& WorldAxis, float WorldExtent, const FMatrix& ClothWorldInverse, FClothCollisionData& OutData, uint32& ConvexMask)
{
	FVector LocalAxis = TransformWorldVectorToClothLocal(ClothWorldInverse, WorldCenter, WorldAxis * WorldExtent);
	const float LocalExtent = LocalAxis.Length();
	if (LocalExtent <= 1.0e-4f)
	{
		return;
	}
	LocalAxis.Normalize();

	const FVector LocalCenter = TransformWorldPositionToClothLocal(ClothWorldInverse, WorldCenter);
	AppendPlaneToConvex(LocalAxis, LocalCenter + LocalAxis * LocalExtent, OutData, ConvexMask);
	AppendPlaneToConvex(LocalAxis * -1.0f, LocalCenter - LocalAxis * LocalExtent, OutData, ConvexMask);
}

void FClothCollisionBuilder::AppendBoxFromWorldShape(
	const FVector& WorldCenter,
	const FVector& WorldForward,
	const FVector& WorldRight,
	const FVector& WorldUp,
	const FVector& WorldExtent,
	float CollisionThickness,
	const FMatrix& ClothWorldInverse,
	FClothCollisionData& OutData)
{
	if (OutData.Planes.size() + 6 > MaxClothCollisionPlanes)
	{
		return;
	}

	uint32 ConvexMask = 0;
	const FVector InflatedWorldExtent(
		(std::max)(0.0f, WorldExtent.X + CollisionThickness),
		(std::max)(0.0f, WorldExtent.Y + CollisionThickness),
		(std::max)(0.0f, WorldExtent.Z + CollisionThickness));

	AppendBoxAxisPlanes(WorldCenter, WorldForward, InflatedWorldExtent.X, ClothWorldInverse, OutData, ConvexMask);
	AppendBoxAxisPlanes(WorldCenter, WorldRight, InflatedWorldExtent.Y, ClothWorldInverse, OutData, ConvexMask);
	AppendBoxAxisPlanes(WorldCenter, WorldUp, InflatedWorldExtent.Z, ClothWorldInverse, OutData, ConvexMask);

	if (ConvexMask != 0)
	{
		OutData.ConvexMasks.push_back(ConvexMask);
	}
}
