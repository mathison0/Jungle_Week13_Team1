#include "Physics/PhysX/PhysXClothCollisionReader.h"

#include "Cloth/ClothCollisionBuilder.h"
#include "Math/Vector.h"
#include "Physics/PhysX/PhysXHelper.h"

#include <PxPhysicsAPI.h>

void FPhysXClothCollisionReader::AppendNvClothCollisionFromPxShape(
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
