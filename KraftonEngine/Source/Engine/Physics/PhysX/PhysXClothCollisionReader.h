#pragma once

#include "Cloth/ClothCollisionTypes.h"
#include "Math/Matrix.h"

namespace physx
{
	class PxRigidActor;
	class PxShape;
}

// 이미 PhysX Scene에 등록된 런타임 Shape를 읽어서 NvCloth 충돌 데이터로 변환한다.
// PhysX Actor/Shape를 새로 만들거나 수정하지 않는다.
class FPhysXClothCollisionReader
{
public:
	static void AppendNvClothCollisionFromPxShape(
		const physx::PxRigidActor* Actor,
		const physx::PxShape* Shape,
		float CollisionThickness,
		const FMatrix& ClothWorldInverse,
		FClothCollisionData& OutData);
};
