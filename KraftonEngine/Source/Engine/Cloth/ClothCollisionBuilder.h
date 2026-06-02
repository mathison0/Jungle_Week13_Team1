#pragma once

#include "Cloth/ClothCollisionTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

// 엔진 월드 공간의 충돌 프리미티브를 NvCloth가 쓰는 cloth-local 충돌 배열로 변환한다.
class FClothCollisionBuilder
{
public:
	static void AppendSphereFromWorldShape(
		const FVector& WorldCenter,
		float WorldRadius,
		float CollisionThickness,
		const FMatrix& ClothWorldInverse,
		FClothCollisionData& OutData);

	static void AppendCapsuleFromWorldShape(
		const FVector& WorldCenter,
		const FVector& WorldAxis,
		float WorldRadius,
		float WorldSegmentHalfLength,
		float CollisionThickness,
		const FMatrix& ClothWorldInverse,
		FClothCollisionData& OutData);

	static void AppendBoxFromWorldShape(
		const FVector& WorldCenter,
		const FVector& WorldForward,
		const FVector& WorldRight,
		const FVector& WorldUp,
		const FVector& WorldExtent,
		float CollisionThickness,
		const FMatrix& ClothWorldInverse,
		FClothCollisionData& OutData);

private:
	static constexpr uint32 MaxClothCollisionSpheres = 32;
	static constexpr uint32 MaxClothCollisionCapsules = 16;
	static constexpr uint32 MaxClothCollisionPlanes = 32;

	static FVector TransformWorldPositionToClothLocal(const FMatrix& ClothWorldInverse, const FVector& WorldPosition);
	static FVector TransformWorldVectorToClothLocal(const FMatrix& ClothWorldInverse, const FVector& WorldOrigin, const FVector& WorldVector);
	static float TransformWorldRadiusToClothLocal(const FMatrix& ClothWorldInverse, const FVector& WorldCenter, float WorldRadius);
	static void AppendPlaneToConvex(const FVector& Normal, const FVector& Point, FClothCollisionData& OutData, uint32& ConvexMask);
	static void AppendBoxAxisPlanes(const FVector& WorldCenter, const FVector& WorldAxis, float WorldExtent, const FMatrix& ClothWorldInverse, FClothCollisionData& OutData, uint32& ConvexMask);
};
