#pragma once

namespace physx
{
	class PxFoundation;
	class PxPhysics;

#ifdef _DEBUG
	class PxPvd;
	class PxPvdTransport;
#endif
}

// ============================================================
// FPhysXCore
//
// 프로세스 단위로 공유되는 PhysX Foundation, Physics, Extensions를 관리한다.
// Scene은 World마다 만들지만, PhysX Core 객체는 중복 생성하지 않는다.
// ============================================================
namespace FPhysXCore
{
#ifdef _DEBUG
	bool Acquire(physx::PxFoundation*& OutFoundation, physx::PxPhysics*& OutPhysics,
		physx::PxPvd*& OutPvd, physx::PxPvdTransport*& OutPvdTransport);
#else
	bool Acquire(physx::PxFoundation*& OutFoundation, physx::PxPhysics*& OutPhysics);
#endif

	void Release();
}
