#pragma once

#include "Cloth/ClothCollisionBuilder.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Physics/PhysX/PhysXHelper.h"

#include <PxPhysicsAPI.h>

// 이미 PhysX Scene에 등록된 런타임 Shape를 읽어서 NvCloth 충돌 프리미티브로 추가한다.
// PhysX Actor/Shape를 새로 만들거나 수정하지 않는다.
class FPhysXClothCollisionReader
{
public:
	static void AppendNvClothCollisionFromPxShape(
		const physx::PxRigidActor* Actor,
		const physx::PxShape* Shape,
		float CollisionThickness,
		const FMatrix& ClothWorldInverse,
		FClothCollisionData& OutData)
	{
		if (!Actor || !Shape)
		{
			return;
		}

		const physx::PxTransform ShapePose = physx::PxShapeExt::getGlobalPose(*Shape, *Actor);
		const FVector WorldCenter = FPhysXHelper::ToFVector(ShapePose.p);
		const physx::PxGeometryHolder Geometry = Shape->getGeometry();

		switch (Geometry.getType())
		{
		case physx::PxGeometryType::eSPHERE:
			FClothCollisionBuilder::AppendSphereFromWorldShape(
				WorldCenter,
				Geometry.sphere().radius,
				CollisionThickness,
				ClothWorldInverse,
				OutData);
			break;
		case physx::PxGeometryType::eCAPSULE:
			FClothCollisionBuilder::AppendCapsuleFromWorldShape(
				WorldCenter,
				FPhysXHelper::ToFVector(ShapePose.q.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f))),
				Geometry.capsule().radius,
				Geometry.capsule().halfHeight,
				CollisionThickness,
				ClothWorldInverse,
				OutData);
			break;
		case physx::PxGeometryType::eBOX:
			FClothCollisionBuilder::AppendBoxFromWorldShape(
				WorldCenter,
				FPhysXHelper::ToFVector(ShapePose.q.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f))),
				FPhysXHelper::ToFVector(ShapePose.q.rotate(physx::PxVec3(0.0f, 1.0f, 0.0f))),
				FPhysXHelper::ToFVector(ShapePose.q.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f))),
				FPhysXHelper::ToFVector(Geometry.box().halfExtents),
				CollisionThickness,
				ClothWorldInverse,
				OutData);
			break;
		default:
			break;
		}
	}
};
