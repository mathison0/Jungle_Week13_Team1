#pragma once

namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxAllocatorCallback;
	class PxErrorCallback;

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

	// NvCloth 등 PhysX와 같은 메모리/에러 인프라를 공유해야 하는 SDK용.
	// NvClothInitialize에 이 콜백들을 넘기면 PhysX와 동일한 allocator/error 경로를 쓴다.
	// (PxAssertHandler는 physx::PxGetAssertHandler() 전역을 직접 사용)
	physx::PxAllocatorCallback& GetAllocatorCallback();
	physx::PxErrorCallback& GetErrorCallback();
}
