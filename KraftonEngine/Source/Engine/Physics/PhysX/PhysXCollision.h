#pragma once

#include <PxPhysicsAPI.h>

struct FPhysXShapeCollisionDesc;

// ============================================================
// FPhysXCollision
//
// 엔진의 Collision Channel/Response 규칙을 PhysX filter data와
// filter shader로 변환한다.
// ============================================================
namespace FPhysXCollision
{
	void SetupFilterData(physx::PxShape* Shape, const FPhysXShapeCollisionDesc& Collision);

	physx::PxFilterFlags FilterShader(
		physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData FilterData0,
		physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData FilterData1,
		physx::PxPairFlags& PairFlags,
		const void* ConstantBlock,
		physx::PxU32 ConstantBlockSize);
}
