#include "PhysXCore.h"

#include "Core/Logging/Log.h"
#include "Core/Types/CoreTypes.h"

#include <vector>

#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleSDK.h>

using namespace physx;

#ifdef _DEBUG
// PVD 초기화, 기본 비활성화
static constexpr bool GEnablePhysXPvd = false;

// PVD 기본 포트. NVIDIA PVD 기본 포트 5425
static constexpr const char* GPhysXPvdHost = "127.0.0.1";
static constexpr int32 GPhysXPvdPort = 5425;
static constexpr uint32 GPhysXPvdTimeoutMs = 1000;
#endif

class FPhysXErrorCallback : public PxErrorCallback
{
public:
	void reportError(PxErrorCode::Enum code, const char* message,
		const char* file, int line) override
	{
		const char* severity = "Info";
		if (code == PxErrorCode::eABORT || code == PxErrorCode::eOUT_OF_MEMORY)
			severity = "Fatal";
		else if (code == PxErrorCode::eINTERNAL_ERROR || code == PxErrorCode::eINVALID_OPERATION)
			severity = "Error";
		else if (code == PxErrorCode::eINVALID_PARAMETER || code == PxErrorCode::ePERF_WARNING)
			severity = "Warning";
		else if (code == PxErrorCode::eDEBUG_WARNING)
			severity = "Warning";

		UE_LOG("[PhysX %s] %s (%s:%d)", severity, message, file, line);
	}
};

static FPhysXErrorCallback GPhysXErrorCallback;
static PxDefaultAllocator GPhysXAllocator;

static PxFoundation* GSharedFoundation = nullptr;
static PxPhysics* GSharedPhysics = nullptr;
#ifdef _DEBUG
static PxPvd* GSharedPvd = nullptr;
static PxPvdTransport* GSharedPvdTransport = nullptr;
#endif

static int32 GSharedRefCount = 0;
static bool GSharedExtensionsInitialized = false;
static bool GSharedVehicleSDKInitialized = false;

// Physics가 실제로 파괴되기 직전(refcount 0)에 호출되는 콜백들.
// PxMaterial 등 Physics가 만든 객체를 캐시한 시스템이 핸들을 무효화하는 데 쓴다.
static std::vector<void(*)()> GTeardownCallbacks;

#ifdef _DEBUG
static void ReleasePvd()
{
	if (GSharedPvd) { GSharedPvd->release(); GSharedPvd = nullptr; }
}

static void ReleasePvdTransport()
{
	if (GSharedPvdTransport) { GSharedPvdTransport->release(); GSharedPvdTransport = nullptr; }
}
#endif

static void ReleaseFoundation()
{
	if (GSharedFoundation) { GSharedFoundation->release(); GSharedFoundation = nullptr; }
}

static void ReleasePhysics()
{
	if (GSharedPhysics) { GSharedPhysics->release(); GSharedPhysics = nullptr; }
}

#ifdef _DEBUG
static void TryCreateSharedPvd()
{
	if (!GEnablePhysXPvd) return;
	if (!GSharedFoundation) return;
	if (GSharedPvd) return;

	GSharedPvd = PxCreatePvd(*GSharedFoundation);
	if (!GSharedPvd)
	{
		UE_LOG("[PhysX] PVD Creation Failed. Continue without PVD.");
		return;
	}

	GSharedPvdTransport = PxDefaultPvdSocketTransportCreate(
		GPhysXPvdHost,
		GPhysXPvdPort,
		GPhysXPvdTimeoutMs
	);

	if (!GSharedPvdTransport)
	{
		UE_LOG("[PhysX] PVD Transport Creation Failed. Continue without PVD.");
		ReleasePvd();
		return;
	}

	const PxPvdInstrumentationFlags Flags =
		PxPvdInstrumentationFlag::eDEBUG |
		PxPvdInstrumentationFlag::ePROFILE |
		PxPvdInstrumentationFlag::eMEMORY;

	const bool bConnected = GSharedPvd->connect(*GSharedPvdTransport, Flags);
	if (!bConnected)
	{
		UE_LOG("[PhysX] PVD Connection Failed. Continue without PVD.");
		ReleasePvdTransport();
		ReleasePvd();
		return;
	}

	UE_LOG("[PhysX] PVD Connected (%s:%d)", GPhysXPvdHost, GPhysXPvdPort);
}
#endif

#ifdef _DEBUG
bool FPhysXCore::Acquire(PxFoundation*& OutFoundation, PxPhysics*& OutPhysics,
	PxPvd*& OutPvd, PxPvdTransport*& OutPvdTransport)
#else
bool FPhysXCore::Acquire(PxFoundation*& OutFoundation, PxPhysics*& OutPhysics)
#endif
{
	if (GSharedRefCount == 0)
	{
		GSharedFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GPhysXAllocator, GPhysXErrorCallback);
		if (!GSharedFoundation)
		{
			UE_LOG("[PhysX] Failed to Create PxFoundation");
			return false;
		}

#ifdef _DEBUG
		TryCreateSharedPvd();
		GSharedPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *GSharedFoundation, PxTolerancesScale(), true, GSharedPvd);
#else
		GSharedPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *GSharedFoundation, PxTolerancesScale(), true, nullptr);
#endif
		if (!GSharedPhysics)
		{
			UE_LOG("[PhysX] Failed to Create PxPhysics.");
#ifdef _DEBUG
			ReleasePvdTransport();
			ReleasePvd();
#endif
			ReleaseFoundation();
			return false;
		}

#ifdef _DEBUG
		if (!PxInitExtensions(*GSharedPhysics, GSharedPvd))
#else
		if (!PxInitExtensions(*GSharedPhysics, nullptr))
#endif
		{
			UE_LOG("[PhysX] PxInitExtensions failed");

			ReleasePhysics();
#ifdef _DEBUG
			ReleasePvdTransport();
			ReleasePvd();
#endif
			ReleaseFoundation();
			return false;
		}

		GSharedExtensionsInitialized = true;

		// Vehicle SDK — 공유 PxPhysics당 한 번 초기화 (Extension 이후). Z-up / X-forward / velocity-change.
		// PxVehicle* 직접 제어는 FPhysXPhysicsScene::GetPxScene / GetComponentRigidActor 게이트로 접근한다.
		if (PxInitVehicleSDK(*GSharedPhysics))
		{
			PxVehicleSetBasisVectors(PxVec3(0.0f, 0.0f, 1.0f), PxVec3(1.0f, 0.0f, 0.0f));
			PxVehicleSetUpdateMode(PxVehicleUpdateMode::eVELOCITY_CHANGE);
			GSharedVehicleSDKInitialized = true;
		}
		else
		{
			UE_LOG("[PhysX] PxInitVehicleSDK failed (deactivate vehicle)");
		}

		UE_LOG("[PhysX] Shared Foundation / Physics / Extension / Vehicle Initialized!");
	}

	++GSharedRefCount;
	OutFoundation = GSharedFoundation;
	OutPhysics = GSharedPhysics;
#ifdef _DEBUG
	OutPvd = GSharedPvd;
	OutPvdTransport = GSharedPvdTransport;
#endif

	return true;
}

void FPhysXCore::Release()
{
	if (GSharedRefCount <= 0) { GSharedRefCount = 0; return; }
	--GSharedRefCount;
	if (GSharedRefCount > 0) return;

	// Physics가 곧 파괴된다. Physics가 만든 객체(PxMaterial 등)를 캐시한 시스템이 dangling
	// 포인터를 들지 않도록, Physics가 아직 살아있는 지금 핸들을 무효화하게 한다.
	for (void (*Callback)() : GTeardownCallbacks)
	{
		if (Callback) Callback();
	}

	// Vehicle SDK는 Extension/Physics에 의존하므로 그보다 먼저 닫는다.
	if (GSharedVehicleSDKInitialized)
	{
		PxCloseVehicleSDK();
		GSharedVehicleSDKInitialized = false;
	}

	if (GSharedExtensionsInitialized)
	{
		PxCloseExtensions();
		GSharedExtensionsInitialized = false;
		UE_LOG("[PhysX] Extension Closed");
	}

	ReleasePhysics();

#ifdef _DEBUG
	if (GSharedPvd && GSharedPvd->isConnected())
	{
		GSharedPvd->disconnect();
	}

	ReleasePvdTransport();
	ReleasePvd();
#endif

	ReleaseFoundation();

	GSharedRefCount = 0;
	UE_LOG("[PhysX] Shared Foundation / Physics released.");
}

// NvCloth 등 외부 SDK가 PhysX와 같은 allocator/error 콜백을 공유하도록 노출한다.
// 이 콜백들은 file-static으로 항상 살아있어 Acquire 전후 어디서든 안전하게 참조 가능.
physx::PxAllocatorCallback& FPhysXCore::GetAllocatorCallback()
{
	return GPhysXAllocator;
}

physx::PxErrorCallback& FPhysXCore::GetErrorCallback()
{
	return GPhysXErrorCallback;
}

// 프로세스 수명 keepalive — out 핸들이 필요 없는 호출자(엔진)가 refcount만 올릴 때.
bool FPhysXCore::AcquireKeepAlive()
{
	PxFoundation* F = nullptr;
	PxPhysics* P = nullptr;
#ifdef _DEBUG
	PxPvd* Pvd = nullptr;
	PxPvdTransport* T = nullptr;
	return Acquire(F, P, Pvd, T);
#else
	return Acquire(F, P);
#endif
}

void FPhysXCore::RegisterTeardownCallback(void (*Callback)())
{
	if (Callback)
	{
		GTeardownCallbacks.push_back(Callback);
	}
}
