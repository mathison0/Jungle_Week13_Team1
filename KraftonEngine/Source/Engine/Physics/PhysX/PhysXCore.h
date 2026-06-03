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

	// 프로세스 수명 동안 코어를 잡아 두기 위한 keepalive (out 핸들 불필요).
	// 씬 전환 사이 refcount가 0으로 떨어져 Physics가 파괴/재생성되는 것을 막는다.
	bool AcquireKeepAlive();

	// 코어(Physics)가 실제로 teardown되기 직전(refcount 0) 호출되는 콜백 등록.
	// PxMaterial 등 Physics가 만든 객체를 캐시한 시스템이 핸들을 무효화하는 용도.
	void RegisterTeardownCallback(void (*Callback)());
}
