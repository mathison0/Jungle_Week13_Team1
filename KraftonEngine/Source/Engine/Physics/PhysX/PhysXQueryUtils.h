#pragma once

#include "Core/Types/CollisionTypes.h"
#include "Math/Vector.h"

#include <PxPhysicsAPI.h>

// ============================================================
// FPhysXQueryUtils
//
// PhysX query hit을 엔진 FHitResult로 채운다.
// Raycast와 sweep의 중복 결과 변환 코드를 한 곳에 둔다.
// ============================================================
namespace FPhysXQueryUtils
{
	void FillRaycastHit(const physx::PxRaycastHit& Block, FHitResult& OutHit);
	void FillSweepHit(const physx::PxSweepHit& Block, const FVector& Start, const FVector& Dir, FHitResult& OutHit);
}
