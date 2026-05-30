#include "Physics/PhysXPhysicsScene.h"
#include "Physics/PhysXHelper.h"
#include "Component/PrimitiveComponent.h"
#include "Component/ShapeComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Component/Shape/SphereComponent.h"
#include "Component/Shape/CapsuleComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Math/Quat.h"
#include "Object/Object.h"  // IsAliveObject
#include "Core/Logging/Log.h"
#include "Physics/PhysicalMaterial.h"
#include "Math/MathUtils.h"

// PhysX headers
#include <PxPhysicsAPI.h>

#include <algorithm>
#include <memory>

using namespace physx;

// ============================================================
// PhysX Core Settings
// ============================================================
#ifdef _DEBUG
// PVD 초기화, 기본 비활성화
static constexpr bool GEnablePhysXPvd = false;

// PVD 기본 포트. NVIDIA PVD 기본 포트 5425
static constexpr const char* GPhysXPvdHost = "127.0.0.1";
static constexpr int32 GPhysXPvdPort = 5425;
static constexpr uint32 GPhysXPvdTimeoutMs = 1000;
#endif

// Dispatcher Thread 수
// TODO: 프로젝트 세팅으로 빼서 개수 조절할 수 있게 만들기 고려
static constexpr int32 GPhysXWorkerThreadCount = 2;

// ============================================================
// PhysX Error Callback
// ============================================================
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

// ============================================================
// Shared PhysX Core
// 
// PhysX Foundation / Physics는 프로세스 단위로 공유
// 여러 World / 여러 PhysicsScene이 생겨도 중복 생성하지 않음.
// ============================================================
static PxFoundation* GSharedFoundation = nullptr;
static PxPhysics* GSharedPhysics = nullptr;
#ifdef _DEBUG
static PxPvd* GSharedPvd = nullptr;
static PxPvdTransport* GSharedPvdTransport = nullptr;
#endif

static int32 GSharedRefCount = 0;
static bool GSharedExtensionsInitialized = false;

// Release Helper Function
// Fallback으로 해제할 일이 너무 많아서 해제 함수를 따로 개설
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
static void TryCreatedSharedPvd()
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


// Foundation, Physics, Extensions
#ifdef _DEBUG
static bool AcquireSharedPhysX(PxFoundation*& OutFoundation, PxPhysics*& OutPhysics, PxPvd*& OutPvd, PxPvdTransport*& OutPvdTransport)
#else
static bool AcquireSharedPhysX(PxFoundation*& OutFoundation, PxPhysics*& OutPhysics)
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
		TryCreatedSharedPvd();

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

		// Joint, Constraint, Vehicle Extension 전용 확장
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
		UE_LOG("[PhysX] Shared Foundation / Physics / Extension Initialized!");
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

// 마지막 Scene이 사라질 때만 실제 PhysX 객체 해제
static void ReleaseSharedPhysX()
{
	if (GSharedRefCount <= 0) { GSharedRefCount = 0; return; }
	--GSharedRefCount;
	if (GSharedRefCount > 0) return;
	
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

// ============================================================
// PhysX Simulation Event Callback
//
// PhysX 의 onContact / onTrigger 는 Scene->fetchResults(true) 진행 중에 호출되며,
// 그 안에서 직접 게임 측 핸들러(NotifyComponentHit 등)를 호출하면 핸들러가
// World->DestroyActor 같은 scene-mutating 작업을 해서 fetchResults 와 겹쳐 크래쉬한다.
//
// 따라서 콜백은 이벤트를 큐에 적재만 하고, FPhysXPhysicsScene::Tick 의 post-simulate
// 단계 끝에서 DispatchPendingEvents 가 한꺼번에 게임 측 Notify 를 호출한다. 이 시점은
// simulate/fetchResults 외부이므로 핸들러가 자유롭게 actor/component 를 추가/제거해도 안전.
// ============================================================
class FPhysXSimulationCallback : public PxSimulationEventCallback
{
public:
	struct FQueuedHit
	{
		UPrimitiveComponent* Self      = nullptr;  // Notify 가 호출되는 대상
		UPrimitiveComponent* Other     = nullptr;
		FVector              NormalImpulse{0,0,0};
		FHitResult           Hit;
		bool                 bBegin = true;       // false = end
	};

	struct FQueuedTrigger
	{
		UPrimitiveComponent* Self  = nullptr;
		UPrimitiveComponent* Other = nullptr;
		bool                 bBegin = true;        // false = end
	};

	// Block 접촉 → 큐에 적재
	void onContact(const PxContactPairHeader& PairHeader,
		const PxContactPair* Pairs, PxU32 Count) override
	{
		if (PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_0
			|| PairHeader.flags & PxContactPairHeaderFlag::eREMOVED_ACTOR_1)
			return;

		for (PxU32 i = 0; i < Count; ++i)
		{
			const PxContactPair& CP = Pairs[i];
			const bool bBegin = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND);
			const bool bEnd = CP.events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST);
			if (!bBegin && !bEnd) continue;

			FBodyInstance* BodyA = FPhysXHelper::GetBodyInstanceFromPxShape(CP.shapes[0]);
			FBodyInstance* BodyB = FPhysXHelper::GetBodyInstanceFromPxShape(CP.shapes[1]);
			UPrimitiveComponent* CompA = BodyA ? BodyA->GetOwnerComponent() : nullptr;
			UPrimitiveComponent* CompB = BodyB ? BodyB->GetOwnerComponent() : nullptr;
			if (!CompA || !CompB) continue;

			if (bEnd)
			{
				FQueuedHit A;
				A.Self = CompA;
				A.Other = CompB;
				A.bBegin = false;
				PendingHits.push_back(A);

				FQueuedHit B;
				B.Self = CompB;
				B.Other = CompA;
				B.bBegin = false;
				PendingHits.push_back(B);
				continue;
			}

			// Contact point — 큐 dispatch 시점에 PxContactPair 가 이미 무효이므로 여기서 모두 추출.
			PxContactPairPoint ContactPoints[1];
			PxU32 NumPoints = CP.extractContacts(ContactPoints, 1);

			FVector ContactPos(0, 0, 0);
			FVector ContactNormal(0, 0, 1);
			float Penetration = 0.0f;

			if (NumPoints > 0)
			{
				ContactPos    = FPhysXHelper::ToFVector(ContactPoints[0].position);
				ContactNormal = FPhysXHelper::ToFVector(ContactPoints[0].normal);
				Penetration   = ContactPoints[0].separation; // 음수 = 관통
			}

			const FVector NormalImpulse = ContactNormal * Penetration;

			FQueuedHit A;
			A.Self                = CompA;
			A.Other               = CompB;
			A.NormalImpulse       = NormalImpulse;
			A.Hit.bHit            = true;
			A.Hit.HitComponent    = CompB;
			A.Hit.HitActor        = CompB->GetOwner();
			A.Hit.WorldHitLocation= ContactPos;
			A.Hit.ImpactNormal    = ContactNormal;
			A.Hit.WorldNormal     = ContactNormal;
			A.Hit.PenetrationDepth= -Penetration;
			PendingHits.push_back(A);

			FQueuedHit B;
			B.Self                 = CompB;
			B.Other                = CompA;
			B.NormalImpulse        = NormalImpulse * -1.0f;
			B.Hit.bHit             = true;
			B.Hit.HitComponent     = CompA;
			B.Hit.HitActor         = CompA->GetOwner();
			B.Hit.WorldHitLocation = ContactPos;
			B.Hit.ImpactNormal     = ContactNormal * -1.0f;
			B.Hit.WorldNormal      = ContactNormal * -1.0f;
			B.Hit.PenetrationDepth = -Penetration;
			PendingHits.push_back(B);
		}
	}

	// Trigger 진입/이탈 → 큐에 적재
	void onTrigger(PxTriggerPair* Pairs, PxU32 Count) override
	{
		for (PxU32 i = 0; i < Count; ++i)
		{
			const PxTriggerPair& TP = Pairs[i];

			if (TP.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
				continue;

			FBodyInstance* TriggerBody = FPhysXHelper::GetBodyInstanceFromPxShape(TP.triggerShape);
			FBodyInstance* OtherBody   = FPhysXHelper::GetBodyInstanceFromPxShape(TP.otherShape);
			UPrimitiveComponent* TriggerComp = TriggerBody ? TriggerBody->GetOwnerComponent() : nullptr;
			UPrimitiveComponent* OtherComp   = OtherBody ? OtherBody->GetOwnerComponent() : nullptr;
			if (!TriggerComp || !OtherComp) continue;

			const bool bBegin = (TP.status == PxPairFlag::eNOTIFY_TOUCH_FOUND);
			const bool bEnd   = (TP.status == PxPairFlag::eNOTIFY_TOUCH_LOST);
			if (!bBegin && !bEnd) continue;

			if (TriggerComp->GetGenerateOverlapEvents())
			{
				PendingTriggers.push_back({ TriggerComp, OtherComp, bBegin });
			}
			if (OtherComp->GetGenerateOverlapEvents())
			{
				PendingTriggers.push_back({ OtherComp, TriggerComp, bBegin });
			}
		}
	}

	// FPhysXPhysicsScene::Tick 끝에서 호출. simulate/fetchResults 바깥이므로 핸들러가
	// 자유롭게 World->DestroyActor / SpawnActor / RegisterComponent 호출 가능.
	// 핸들러 도중 다른 컴포넌트가 destroy되는 경우 대비해 dispatch 직전에 IsAliveObject
	// 검증 — destroy된 포인터를 만지지 않는다.
	void DispatchPendingEvents()
	{
		// move-out — dispatch 도중 새 이벤트가 큐에 들어오는 일은 없지만, 안전하게 swap 후 처리.
		std::vector<FQueuedHit> HitsToDispatch;
		HitsToDispatch.swap(PendingHits);
		std::vector<FQueuedTrigger> TriggersToDispatch;
		TriggersToDispatch.swap(PendingTriggers);

		for (FQueuedHit& E : HitsToDispatch)
		{
			if (!IsAliveObject(E.Self) || !IsAliveObject(E.Other)) continue;
			AActor* OtherActor = E.Other->GetOwner();
			if (E.bBegin)
			{
				E.Self->NotifyComponentHit(E.Self, OtherActor, E.Other, E.NormalImpulse, E.Hit);
			}
			else
			{
				E.Self->NotifyComponentEndHit(E.Self, OtherActor, E.Other);
			}
		}

		for (FQueuedTrigger& E : TriggersToDispatch)
		{
			if (!IsAliveObject(E.Self) || !IsAliveObject(E.Other)) continue;
			AActor* OtherActor = E.Other->GetOwner();
			if (E.bBegin)
			{
				FHitResult DummyHit;
				E.Self->NotifyComponentBeginOverlap(E.Self, OtherActor, E.Other, 0, false, DummyHit);
			}
			else
			{
				E.Self->NotifyComponentEndOverlap(E.Self, OtherActor, E.Other, 0);
			}
		}
	}

	void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
	void onWake(PxActor**, PxU32) override {}
	void onSleep(PxActor**, PxU32) override {}
	void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}

private:
	std::vector<FQueuedHit>     PendingHits;
	std::vector<FQueuedTrigger> PendingTriggers;
};

// Compound body의 mass와 center-of-mass를 RootComponent의 값으로 갱신.
// shape 추가/제거 후 inertia 재계산이 필요하므로 RegisterComponent /
// UnregisterComponent 끝에서 호출된다.
static void ApplyRootMassAndCOM(PxRigidDynamic* Dyn, UPrimitiveComponent* Root)
{
	if (!Dyn || !Root) return;
	const float MassKg = (Root->GetMass() > 0.0f) ? Root->GetMass() : 1.0f;
	PxRigidBodyExt::setMassAndUpdateInertia(*Dyn, MassKg);
	Dyn->setCMassLocalPose(PxTransform(FPhysXHelper::ToPxVec3(Root->GetCenterOfMass())));
}

static PxMaterial* TryGetOrCreatePxMaterial(UPrimitiveComponent* Comp, UPhysicalMaterial* DefaultPhysicalMaterial, PxMaterial* DefaultMaterial, PxPhysics* Physics)
{
	if (!Physics) return DefaultMaterial;

	if (Comp)
	{
		if (UPhysicalMaterial* OverrideMaterial = Comp->GetPhysicalMaterialOverride())
		{
			if (PxMaterial* PxMat = OverrideMaterial->GetOrCreatePxMaterial(Physics))
			{
				return PxMat;
			}
		}
	}

	if (DefaultPhysicalMaterial)
	{
		if (PxMaterial* PxMat = DefaultPhysicalMaterial->GetOrCreatePxMaterial(Physics))
		{
			return PxMat;
		}
	}

	return DefaultMaterial;
}

// ============================================================
// Collision Filtering
// ============================================================
// filterData 레이아웃:
//   word0 = 자신의 ObjectType (ECollisionChannel)
//   word1 = Block 비트마스크 (해당 채널에 Block 응답인 비트)
//   word2 = Overlap 비트마스크 (해당 채널에 Overlap 응답인 비트)
//   word3 = 소유 액터 UUID — 같은 액터의 두 컴포넌트끼리 충돌을 무시하기 위함
//           (Native 측 O(N²) 루프의 `if (A->GetOwner() == B->GetOwner()) continue;` 가드와 동일 의미)
//           Owner가 없거나 UUID가 0이면 가드 미적용.

static void SetupFilterData(PxShape* Shape, UPrimitiveComponent* Comp)
{
	PxFilterData Filter;
	Filter.word0 = static_cast<PxU32>(Comp->GetCollisionObjectType());
	Filter.word1 = 0;
	Filter.word2 = 0;
	Filter.word3 = Comp->GetOwner() ? Comp->GetOwner()->GetUUID() : 0;

	for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
	{
		ECollisionResponse R = Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Ch));
		if (R == ECollisionResponse::Block)   Filter.word1 |= (1u << Ch);
		if (R == ECollisionResponse::Overlap) Filter.word2 |= (1u << Ch);
	}

	Shape->setSimulationFilterData(Filter);
	Shape->setQueryFilterData(Filter);
}

// PxFilterShader — 엔진의 채널/응답 매트릭스를 PhysX에서 처리
// 양쪽 모두 상대 채널에 대해 Block이면 물리 충돌, 한쪽이라도 Overlap이면 트리거, 그 외 무시
static PxFilterFlags KraftonFilterShader(
	PxFilterObjectAttributes attributes0, PxFilterData filterData0,
	PxFilterObjectAttributes attributes1, PxFilterData filterData1,
	PxPairFlags& pairFlags, const void* /*constantBlock*/, PxU32 /*constantBlockSize*/)
{
	// 같은 액터(같은 owner UUID)의 두 컴포넌트끼리는 충돌 무시.
	// Native 측 O(N²) 루프의 same-owner 가드와 동일 의미. 차량 차체-바퀴처럼
	// 한 액터가 여러 콜라이더를 가질 때 자기끼리 충돌 시뮬레이션되는 문제를 막는다.
	if (filterData0.word3 != 0 && filterData0.word3 == filterData1.word3)
	{
		return PxFilterFlag::eKILL;
	}

	// 트리거 처리 — 한쪽이라도 트리거면 오버랩 통지만
	if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
	{
		pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}

	PxU32 channelA = filterData0.word0; // A의 ObjectType
	PxU32 channelB = filterData1.word0; // B의 ObjectType

	// A가 B의 채널에 대해 Block인지, B가 A의 채널에 대해 Block인지
	bool bABlocksB = (filterData0.word1 & (1u << channelB)) != 0;
	bool bBBlocksA = (filterData1.word1 & (1u << channelA)) != 0;

	// 양쪽 모두 Block → 물리 충돌 + contact 콜백
	if (bABlocksB && bBBlocksA)
	{
		pairFlags = PxPairFlag::eCONTACT_DEFAULT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST
			| PxPairFlag::eNOTIFY_CONTACT_POINTS;
		return PxFilterFlag::eDEFAULT;
	}

	// 한쪽이라도 Overlap → 겹침 감지만 (물리적 밀어내기 없음).
	// 일반적으로 이 케이스는 위 trigger shape 분기에서 이미 처리되지만, 등록 시점에
	// trigger flag로 분류되지 않은 simulation shape pair인데 응답이 Overlap인 경우의
	// 안전망. eSOLVE_CONTACT 명시 제외 + eDETECT_DISCRETE_CONTACT + NOTIFY로 detection만.
	bool bAOverlapsB = (filterData0.word2 & (1u << channelB)) != 0;
	bool bBOverlapsA = (filterData1.word2 & (1u << channelA)) != 0;

	if (bAOverlapsB || bBOverlapsA)
	{
		pairFlags = PxPairFlag::eDETECT_DISCRETE_CONTACT
			| PxPairFlag::eNOTIFY_TOUCH_FOUND
			| PxPairFlag::eNOTIFY_TOUCH_LOST;
		return PxFilterFlag::eDEFAULT;
	}

	// Ignore — 쌍 완전히 제거
	return PxFilterFlag::eKILL;
}

// ============================================================
// Lifecycle
// ============================================================

void FPhysXPhysicsScene::Initialize(UWorld* InWorld)
{
	World = InWorld;

	// Foundation / Physics — 프로세스 싱글턴 공유
#ifdef _DEBUG
	if (!AcquireSharedPhysX(Foundation, Physics, Pvd, PvdTransport))
#else
	if (!AcquireSharedPhysX(Foundation, Physics))
#endif
	{
		UE_LOG("[PhysX] Failed to acquire shared PhysX Core.");
		return;
	}

	if (!Foundation || !Physics)
	{
		UE_LOG("[PhysX] Failed to create Foundation or Physics");
		return;
	}

	// CPU Dispatcher
	Dispatcher = PxDefaultCpuDispatcherCreate(GPhysXWorkerThreadCount);
	if (!Dispatcher)
	{
		UE_LOG("[PhysX] Failed to create CPU dispatcher.");
		return;
	}

	// Event callback
	EventCallback = new FPhysXSimulationCallback();

	// Scene
	PxSceneDesc SceneDesc(Physics->getTolerancesScale());
	SceneDesc.gravity = PxVec3(0.0f, 0.0f, -9.81f); // Z-up, m 단위
	SceneDesc.cpuDispatcher = Dispatcher;
	SceneDesc.filterShader = KraftonFilterShader;
	SceneDesc.simulationEventCallback = EventCallback;
	SceneDesc.flags |= PxSceneFlag::eENABLE_CCD;			// 빠르게 움직이는 dynamic body가 얇은 collider를 관통하는 문제 감소
	SceneDesc.flags |= PxSceneFlag::eENABLE_PCM;			// 접촉점 안정성을 높여 stacked body, ragdoll contact jitter를 줄이는 데 유리
	SceneDesc.flags |= PxSceneFlag::eENABLE_ACTIVE_ACTORS;	// 전체 body 순회 대신 "움직인 actor"만 동기화

	Scene = Physics->createScene(SceneDesc);

	if (!Scene)
	{
		UE_LOG("[PhysX] Failed to create Scene");
		return;
	}

#ifdef _DEBUG
	// PVD Scene Client 설정
	if (PxPvdSceneClient* PvdClient = Scene->getScenePvdClient())
	{
		PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
		PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
		PvdClient->setScenePvdFlag(PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, false);

		// PVD Settings
		// Scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, 1.0f);				// PVD / PhysX debug visualization Scale
		// Scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);	// Collision shape
		// Scene->setVisualizationParameter(PxVisualizationParameter::eBODY_AXES, 1.0f);			// Actor 축
		// Scene->setVisualizationParameter(PxVisualizationParameter::eBODY_MASS_AXES, 1.0f);		// Body mass axes
		// Scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_NORMAL, 1.0f);		// Contact normal
		// Scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, 1.0f);	// Joint local frame
		// Scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, 1.0f);		// Joint limit
	}
#endif

	// --- Material ---
	
	// Default material (static friction, dynamic friction, restitution)
	// TODO: PhysicalMaterial 구현 후 Fallback으로 만들기
	DefaultPhysicalMaterial = UObjectManager::Get().CreateObject<UPhysicalMaterial>();
	DefaultMaterial = DefaultPhysicalMaterial->GetOrCreatePxMaterial(Physics);
	if (!DefaultMaterial)
	{
		UE_LOG("[PhysX] Failed to Create Default Physical Material");
		return;
	}


	UE_LOG("[PhysX] Initialized successfully (Scene=%p)", Scene);
}

void FPhysXPhysicsScene::Shutdown()
{
	// Constraint는 PxRigidActor를 참조한다.
	// 따라서 Bodies보다 먼저 release
	for (auto& ConstraintPtr : Constraints)
	{
		if (ConstraintPtr)
		{
			ConstraintPtr->TerminateConstraint();
		}
	}
	Constraints.clear();

	// Body 정리
	for (auto& MappingPtr : BodyMappings)
	{
		if (!MappingPtr) continue;
		FBodyMapping& Mapping = *MappingPtr;

		if (Mapping.Actor)
		{
			for (UPrimitiveComponent* Component : Mapping.Components)
			{
				if (Component)
				{
					Component->GetBodyInstance()->TerminateBody();
				}
			}
			if (Mapping.RootComp)
			{
				Mapping.RootComp->GetBodyInstance()->TerminateBody();
			}

			if (Scene)
			{
				Scene->removeActor(*Mapping.Actor);
			}
			Mapping.Actor->release();
			Mapping.Actor = nullptr;
		}
	}
	BodyMappings.clear();

	if (DefaultPhysicalMaterial)
	{
		UObjectManager::Get().DestroyObject(DefaultPhysicalMaterial);
		DefaultPhysicalMaterial = nullptr;
	}
	DefaultMaterial = nullptr;

	if (Scene) { Scene->release(); Scene = nullptr; }
	if (EventCallback) { delete EventCallback; EventCallback = nullptr; }
	if (Dispatcher) { Dispatcher->release(); Dispatcher = nullptr; }

	// Foundation/Physics는 공유 싱글턴 — release 카운트 감소만
	Foundation = nullptr;
	Physics = nullptr;
#ifdef _DEBUG
	Pvd = nullptr;
	PvdTransport = nullptr;
#endif
	ReleaseSharedPhysX();

	World = nullptr;

	UE_LOG("[PhysX] Scene shutdown complete.");
}

// ============================================================
// Body 관리 — Actor 단위 compound
//
// 한 액터의 여러 PrimitiveComponent는 같은 PxRigidActor에 shape로 합쳐진다.
// shape의 LocalPose는 액터 RootComponent에 대한 상대 transform.
// userData: PxActor → FBodyInstance, PxShape → FBodyInstance.
// ============================================================

void FPhysXPhysicsScene::RegisterComponent(UPrimitiveComponent* Comp)
{
	if (!Comp || !Scene || !Physics || !DefaultMaterial) return;
	if (FindMappingByComponent(Comp)) return; // 이미 등록됨

	AActor* OwnerActor = Comp->GetOwner();
	if (!OwnerActor) return;

	FBodyMapping* Mapping = FindMappingByActor(OwnerActor);

	if (!Mapping)
	{
		UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent());
		if (!RootPrim) RootPrim = Comp;

		const bool bDynamic = RootPrim->GetSimulatePhysics();
		PxTransform BodyXf = FPhysXHelper::ToPxTransform(RootPrim);

		PxRigidActor* Body = bDynamic
			? static_cast<PxRigidActor*>(Physics->createRigidDynamic(BodyXf))
			: static_cast<PxRigidActor*>(Physics->createRigidStatic(BodyXf));
		if (!Body) return;

		auto NewMapping = std::make_unique<FBodyMapping>();

		NewMapping->OwnerActor = OwnerActor;
		NewMapping->Actor = Body;
		NewMapping->RootComp = RootPrim;

		// 현재 compound actor의 대표 BodyInstance는 RootComponent가 소유한다.
		// 각 shape component도 같은 PxActor를 가리키는 자신의 BodyInstance를 가진다.
		RootPrim->GetBodyInstance()->InitBody(RootPrim, Body);

		// PxActor::userData는 이제 AActor*가 아니라 FBodyInstance*이다.
		FPhysXHelper::SetUserData(Body, RootPrim->GetBodyInstance());

		Scene->addActor(*Body);

		BodyMappings.push_back(std::move(NewMapping));
		Mapping = BodyMappings.back().get();
	}

	// shape 추가
	PxShape* Shape = AddShapeForComponent(*Mapping, Comp);
	if (!Shape) return;
	Comp->GetBodyInstance()->InitBody(Comp, Mapping->Actor);
	Mapping->Components.push_back(Comp);

	// Dynamic이면 RootComp의 Mass / CenterOfMass로 갱신 (shape 추가될 때마다 inertia 재계산).
	if (PxRigidDynamic* Dyn = Mapping->Actor->is<PxRigidDynamic>())
	{
		ApplyRootMassAndCOM(Dyn, Mapping->RootComp);
	}
}

void FPhysXPhysicsScene::UnregisterComponent(UPrimitiveComponent* Comp)
{
	if (!Comp || !Scene) return;

	FBodyMapping* Mapping = FindMappingByComponent(Comp);
	if (!Mapping) return;

	FBodyInstance* ComponentBody = Comp->GetBodyInstance();
	DestroyConstraintsForBody(ComponentBody);

	// 해당 컴포넌트의 shape detach
	DetachShapeForComponent(*Mapping, Comp);

	// Components 배열에서 제거
	Mapping->Components.erase(
		std::remove(Mapping->Components.begin(), Mapping->Components.end(), Comp),
		Mapping->Components.end());

	// 마지막 컴포넌트가 빠지면 actor 자체도 release
	if (Mapping->Components.empty())
	{
		if (Mapping->Actor)
		{
			if (Mapping->RootComp && Mapping->RootComp != Comp)
			{
				DestroyConstraintsForBody(Mapping->RootComp->GetBodyInstance());
				Mapping->RootComp->GetBodyInstance()->TerminateBody();
			}
			if (ComponentBody)
			{
				ComponentBody->TerminateBody();
			}
			
			Scene->removeActor(*Mapping->Actor);
			Mapping->Actor->release();
			Mapping->Actor = nullptr;
		}

		// Mapping 포인터와 같은 unique_ptr을 찾아 제거한다.
		BodyMappings.erase(
			std::remove_if(
				BodyMappings.begin(),
				BodyMappings.end(),
				[Mapping](const std::unique_ptr<FBodyMapping>& Ptr)
				{
					return Ptr.get() == Mapping;
				}
			),
			BodyMappings.end()
		);

		return;
	}

	if (ComponentBody)
	{
		ComponentBody->TerminateBody();
	}

	if (Comp == Mapping->RootComp)
	{
		Mapping->RootComp = Mapping->Components.front();
		FPhysXHelper::SetUserData(Mapping->Actor, Mapping->RootComp ? Mapping->RootComp->GetBodyInstance() : nullptr);
	}

	// 남은 shape가 있으면 mass/inertia 재계산
	if (PxRigidDynamic* Dyn = Mapping->Actor->is<PxRigidDynamic>())
	{
		ApplyRootMassAndCOM(Dyn, Mapping->RootComp);
	}
}

void FPhysXPhysicsScene::RebuildBody(UPrimitiveComponent* Comp)
{
	// SimulatePhysics 변경(Dynamic ↔ Static)은 PxActor type 변경이라 actor를 통째 재생성해야 한다.
	// 또한 ObjectType/Response 변경은 shape filterData도 새로 계산해야 정확.
	// 단순화 위해 같은 액터의 모든 컴포넌트를 unregister + register로 일괄 재구성.
	if (!Comp || !Scene) return;

	AActor* OwnerActor = Comp->GetOwner();
	if (!OwnerActor) return;

	FBodyMapping* Mapping = FindMappingByActor(OwnerActor);
	if (!Mapping) return; // 등록 안 됨 — skip

	// 같은 actor의 모든 컴포넌트 캐시 (unregister가 mapping을 제거할 수 있어 미리 복사)
	TArray<UPrimitiveComponent*> CompList = Mapping->Components;

	for (UPrimitiveComponent* C : CompList)
	{
		UnregisterComponent(C);
	}
	for (UPrimitiveComponent* C : CompList)
	{
		RegisterComponent(C);
	}
}

// ============================================================
// Simulation
// ============================================================

void FPhysXPhysicsScene::Tick(float DeltaTime)
{
	if (!Scene || DeltaTime <= 0.0f) return;

	// 어떤 이유로든 frame hitch (씬 로드 / 큰 OBJ 동기 로딩 / Alt-Tab / OS 스파이크) 가
	// 발생해도 PhysX 가 큰 dt 한 번에 적분해 차량·메테오가 콜리전을 뚫는 tunneling 사고를
	// 막기 위한 클램프. 0.1s 는 60 m/s 차량이 한 step 에 6m 이동 — 충돌 박스 내에서 풀림
	// 가능한 수준이고, 그 이상 hitch 면 게임을 느리게 진행시키더라도 안전이 우선.
	constexpr float MaxPhysicsDeltaTime = 0.1f;
	if (DeltaTime > MaxPhysicsDeltaTime)
	{
		DeltaTime = MaxPhysicsDeltaTime;
	}

	// ── Pre-simulate: Engine → PhysX Transform 동기화 ──
	// 한 PxActor가 여러 컴포넌트를 가지므로 RootComp 기준으로만 한 번 동기화.
	//
	// Dynamic actor도 Engine 측 transform이 PhysX와 충분히 크게 다르면 teleport한다.
	// (lua spawn 직후 m.Location = pos 같은 외부 변경 흡수용)
	//
	// 정상 시뮬레이션 흐름에서는 post-simulate가 Engine = PhysX로 맞춰주므로
	// 다음 frame pre에서 차이 ≈ 0 → skip. 단 round-trip의 부동소수 오차로 작은
	// 차이는 매 frame 발생할 수 있어 threshold를 충분히 크게 잡아 false-positive
	// teleport를 막는다.
	//
	// velocity는 의도적으로 보존 — PhysX의 정상 시뮬레이션 momentum 유지.
	constexpr float TeleportPosThresholdSq = 1.0f;   // 1m² (1m 이상 차이 시만 teleport)
	constexpr float TeleportRotThreshold = 0.99f;    // ~8° 차이 시만 teleport

	for (auto& MappingPtr : BodyMappings)
	{
		if (!MappingPtr) continue;
		FBodyMapping& Mapping = *MappingPtr;
		if (!Mapping.RootComp || !Mapping.Actor) continue;

		PxTransform NewPose = FPhysXHelper::ToPxTransform(Mapping.RootComp);

		if (PxRigidDynamic* Dynamic = Mapping.Actor->is<PxRigidDynamic>())
		{
			if (Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC)
			{
				Dynamic->setKinematicTarget(NewPose);
			}
			else
			{
				PxTransform PxPose = Dynamic->getGlobalPose();
				PxVec3 dp = NewPose.p - PxPose.p;
				const float DistSq = dp.x * dp.x + dp.y * dp.y + dp.z * dp.z;
				const float QDot = std::abs(
					NewPose.q.x * PxPose.q.x + NewPose.q.y * PxPose.q.y +
					NewPose.q.z * PxPose.q.z + NewPose.q.w * PxPose.q.w);

				if (DistSq > TeleportPosThresholdSq || QDot < TeleportRotThreshold)
				{
					// 큰 외부 변경 → teleport. velocity는 보존.
					Dynamic->setGlobalPose(NewPose);
				}
			}
		}
		else if (Mapping.Actor->is<PxRigidStatic>())
		{
			Mapping.Actor->setGlobalPose(NewPose);
		}
	}

	// ── Simulate ──
	Scene->simulate(DeltaTime);
	Scene->fetchResults(true);

	// ── Post-simulate: PhysX → Engine Transform 동기화 ──
	// RootComp에만 transform 적용 → 자식 컴포넌트는 attach로 자동 따라감.
	for (auto& MappingPtr : BodyMappings)
	{
		if (!MappingPtr) continue;
		FBodyMapping& Mapping = *MappingPtr;

		if (!Mapping.RootComp || !Mapping.Actor) continue;
		FBodyInstance* RootBodyInstance = Mapping.RootComp->GetBodyInstance();
		if (!RootBodyInstance || !RootBodyInstance->IsDynamic()) continue;
		if (RootBodyInstance->IsKinematic()) continue;
		if (RootBodyInstance->IsInstanceSleeping()) continue;

		FVector NewPos = RootBodyInstance->GetEngineWorldLocation();
		FQuat NewRot = RootBodyInstance->GetEngineWorldRotation();

		Mapping.RootComp->SetWorldLocation(NewPos);
		Mapping.RootComp->SetRelativeRotation(NewRot);
	}

	// ── Dispatch deferred contact/trigger events ──
	// onContact / onTrigger 는 fetchResults 안에서 fire 되므로 거기서 직접 게임 핸들러를
	// 부르면 핸들러의 World->DestroyActor 등이 PhysX scene 변경 타이밍과 겹쳐 크래쉬한다.
	// 그래서 큐에만 적재했고, 이 시점(simulate/fetchResults 외부)에서 한꺼번에 dispatch.
	if (EventCallback)
	{
		EventCallback->DispatchPendingEvents();
	}
}

// ============================================================
// Internal helpers
// ============================================================

static PxTransform BuildComponentLocalPose(UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp)
{
	PxTransform LocalPose(PxIdentity);
	if (!Comp || Comp == RootComp || !RootComp)
	{
		return LocalPose;
	}

	FVector RootPos = RootComp->GetWorldLocation();
	FQuat RootRot = RootComp->GetWorldMatrix().ToQuat();
	FVector CompPos = Comp->GetWorldLocation();
	FQuat CompRot = Comp->GetWorldMatrix().ToQuat();

	FQuat InvRootRot = RootRot.Inverse();
	FVector LocalPos = InvRootRot.RotateVector(CompPos - RootPos);
	FQuat LocalRot = InvRootRot * CompRot;

	return FPhysXHelper::ToPxTransform(LocalPos, LocalRot);
}

static bool ShouldCreateTriggerShape(UPrimitiveComponent* Comp)
{
	if (!Comp)
	{
		return false;
	}

	// Trigger flag 결정:
	//   1) GenerateOverlapEvents=true (명시적 trigger 의도)  OR
	//   2) 어떤 active 채널에도 Block 응답이 없음 (= simulation 의미 없음, overlap 이벤트만 의도)
	//
	// (2)가 핵심 — FilterShader의 PairFlag만으로는 simulation shape pair에서 contact resolve를
	// 막지 못하는 경우가 있어, 응답이 모두 Overlap/Ignore이면 PhysX shape 자체를 trigger로
	// 등록해 contact resolve 자체가 발생하지 않도록 한다.
	//
	// 같은 PxActor 안에 simulation shape와 trigger shape가 섞이면 PhysX가 거부하므로
	// 같은 액터의 모든 컴포넌트가 같은 종류여야 안전 (현재 ATriggerVolumeBase는 BoxComponent 1개라 OK).
	if (Comp->GetGenerateOverlapEvents())
	{
		return true;
	}

	for (int32 Ch = 0; Ch < static_cast<int32>(ECollisionChannel::ActiveCount); ++Ch)
	{
		if (Comp->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Ch)) == ECollisionResponse::Block)
		{
			return false;
		}
	}
	return true;
}

static void ConfigureCreatedShape(PxShape* Shape, UPrimitiveComponent* Comp, FBodyInstance* BodyInstance, bool bShouldBeTrigger)
{
	if (!Shape || !Comp)
	{
		return;
	}

	SetupFilterData(Shape, Comp);

	if (bShouldBeTrigger)
	{
		Shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
		Shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
	}

	// userData: shape도 FBodyInstance로 매핑한다.
	FPhysXHelper::SetUserData(Shape, BodyInstance);
}

PxShape* FPhysXPhysicsScene::AddShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp)
{
	if (!Mapping.Actor || !DefaultMaterial || !Comp) return nullptr;

	const PxTransform ComponentLocalPose = BuildComponentLocalPose(Mapping.RootComp, Comp);
	const bool bShouldBeTrigger = ShouldCreateTriggerShape(Comp);

	// Shape Component 타입에 따라 PxGeometry 결정
	PxGeometryHolder Geom;
	bool bHasGeom = false;

	// Capsule은 PhysX에서 X축 기준이므로 로컬 회전 보정 필요
	PxQuat ShapeAxisRot = PxQuat(PxIdentity);

	if (auto* Box = Cast<UBoxComponent>(Comp))
	{
		FVector Ext = Box->GetScaledBoxExtent();
		Geom = PxBoxGeometry(Ext.X, Ext.Y, Ext.Z);
		bHasGeom = true;
	}
	else if (auto* Sphere = Cast<USphereComponent>(Comp))
	{
		Geom = PxSphereGeometry(Sphere->GetScaledSphereRadius());
		bHasGeom = true;
	}
	else if (auto* Capsule = Cast<UCapsuleComponent>(Comp))
	{
		float Radius = Capsule->GetScaledCapsuleRadius();
		float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		Geom = PxCapsuleGeometry(Radius, HalfHeight - Radius);
		ShapeAxisRot = PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f));
		bHasGeom = true;
	}

	if (!bHasGeom) return nullptr;

	PxMaterial* ShapeMaterial = TryGetOrCreatePxMaterial(Comp, DefaultPhysicalMaterial, DefaultMaterial, Physics);
	if (!ShapeMaterial)
	{
		UE_LOG("[PhysX] Failed to resolve material for component. Comp=%p", Comp);
		return nullptr;
	}

	PxShape* Shape = PxRigidActorExt::createExclusiveShape(*Mapping.Actor, Geom.any(), *ShapeMaterial);
	if (!Shape) return nullptr;

	// Capsule 등 축 보정을 LocalPose의 회전 부분에 합성
	PxTransform LocalPose = ComponentLocalPose;
	LocalPose.q = LocalPose.q * ShapeAxisRot;
	Shape->setLocalPose(LocalPose);

	ConfigureCreatedShape(Shape, Comp, Comp->GetBodyInstance(), bShouldBeTrigger);

	return Shape;
}

// PxShape::userData는 해당 UPrimitiveComponent가 소유한 FBodyInstance*이다.
// 같은 PxActor에 여러 component shape가 붙어도 body instance 포인터로 component 단위 detach가 가능하다.
void FPhysXPhysicsScene::DetachShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp)
{
	if (!Mapping.Actor || !Comp) return;
	FBodyInstance* ComponentBody = Comp->GetBodyInstance();
	if (!ComponentBody) return;

	const PxU32 NumShapes = Mapping.Actor->getNbShapes();
	if (NumShapes == 0) return;

	std::vector<PxShape*> Shapes(NumShapes);
	Mapping.Actor->getShapes(Shapes.data(), NumShapes);

	for (PxShape* Shape : Shapes)
	{
		if (Shape && FPhysXHelper::HasUserData(Shape, ComponentBody))
		{
			Mapping.Actor->detachShape(*Shape);
		}
	}
}

FPhysXPhysicsScene::FBodyMapping* FPhysXPhysicsScene::FindMappingByActor(AActor* OwnerActor)
{
	for (auto& M : BodyMappings)
	{
		if (M && M->OwnerActor == OwnerActor) return M.get();
	}
	return nullptr;
}

const FPhysXPhysicsScene::FBodyMapping* FPhysXPhysicsScene::FindMappingByActor(AActor* OwnerActor) const
{
	for (const auto& M : BodyMappings)
	{
		if (M && M->OwnerActor == OwnerActor) return M.get();
	}
	return nullptr;
}

// "이 컴포넌트가 shape로 추가된 mapping" 검색 — 등록 가드 + Force/Velocity API 라우팅용.
// owner 기반 lookup과 다름: 같은 owner라도 컴포넌트가 아직 Components에 push되지 않았으면
// 다른 컴포넌트의 shape를 통해 force가 잘못 적용되지 않도록 nullptr 반환.
FPhysXPhysicsScene::FBodyMapping* FPhysXPhysicsScene::FindMappingByComponent(UPrimitiveComponent* Comp)
{
	if (!Comp) return nullptr;

	for (auto& M : BodyMappings)
	{
		if (!M) continue;

		for (UPrimitiveComponent* C : M->Components)
		{
			if (C == Comp)
			{
				return M.get();
			}
		}
	}
	return nullptr;
}

const FPhysXPhysicsScene::FBodyMapping* FPhysXPhysicsScene::FindMappingByComponent(UPrimitiveComponent* Comp) const
{
	if (!Comp) return nullptr;

	for (const auto& M : BodyMappings)
	{
		if (!M) continue;

		for (UPrimitiveComponent* C : M->Components)
		{
			if (C == Comp)
			{
				return M.get();
			}
		}
	}
	return nullptr;
}

// ============================================================
// Force / Torque
// ============================================================

void FPhysXPhysicsScene::AddForce(UPrimitiveComponent* Comp, const FVector& Force)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->AddForce(Force);
}

void FPhysXPhysicsScene::AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->AddForceAtLocation(Force, WorldLocation);
}

void FPhysXPhysicsScene::AddTorque(UPrimitiveComponent* Comp, const FVector& Torque)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->AddTorque(Torque);
}

// ============================================================
// Velocity
// ============================================================

FVector FPhysXPhysicsScene::GetLinearVelocity(UPrimitiveComponent* Comp) const
{
	const FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return FVector(0, 0, 0);
	return BodyInstance->GetLinearVelocity();
}

void FPhysXPhysicsScene::SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->SetLinearVelocity(Vel);
}

FVector FPhysXPhysicsScene::GetAngularVelocity(UPrimitiveComponent* Comp) const
{
	const FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return FVector(0, 0, 0);
	return BodyInstance->GetAngularVelocity();
}

void FPhysXPhysicsScene::SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->SetAngularVelocity(Vel);
}

// ============================================================
// Mass
// ============================================================

void FPhysXPhysicsScene::SetMass(UPrimitiveComponent* Comp, float NewMass)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->SetBodyMass(NewMass);
}

float FPhysXPhysicsScene::GetMass(UPrimitiveComponent* Comp) const
{
	const FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return 1.f;
	return BodyInstance->GetBodyMass();
}

void FPhysXPhysicsScene::SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset)
{
	FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return;
	BodyInstance->SetCenterOfMassLocal(LocalOffset);
}

FVector FPhysXPhysicsScene::GetCenterOfMass(UPrimitiveComponent* Comp) const
{
	const FBodyInstance* BodyInstance = GetBodyInstance(Comp);
	if (!BodyInstance) return FVector(0.f, 0.f, 0.f);
	return BodyInstance->GetCenterOfMassLocal();
}

// ============================================================
// Raycast
// ============================================================

bool FPhysXPhysicsScene::Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	if (!Scene) return false;

	// Channel + IgnoreActor 통합 filter.
	// shape의 queryFilterData는 SetupFilterData에서 word0=ObjectType, word1=Block 마스크.
	// 응답이 TraceChannel에 대해 Block(=word1의 해당 비트 set)인 shape만 hit으로 인정.
	// trigger flag가 set된 shape는 PhysX 측 query에서 자동 제외되므로 별도 처리 불필요.
	struct FChannelRaycastFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;

		FChannelRaycastFilter(const AActor* InIgnoreActor, ECollisionChannel InChannel)
			: IgnoreActor(InIgnoreActor)
			, TraceBit(1u << static_cast<PxU32>(InChannel))
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IgnoreActor && FPhysXHelper::GetOwnerActorFromPxActor(Actor) == IgnoreActor)
			{
				return PxQueryHitType::eNONE;
			}

			// shape의 응답이 TraceChannel에 대해 Block인지 확인.
			// (word1[TraceChannel 비트]가 set이면 Block 응답)
			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				if ((ShapeData.word1 & TraceBit) == 0)
				{
					return PxQueryHitType::eNONE;
				}
			}

			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxRaycastBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FChannelRaycastFilter FilterCallback(IgnoreActor, TraceChannel);

	bool bStatus = Scene->raycast(FPhysXHelper::ToPxVec3(Start), FPhysXHelper::ToPxVec3(Dir), MaxDist, Hit, PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxRaycastHit& Block = Hit.block;
	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = FPhysXHelper::ToFVector(Block.position);
	OutHit.ImpactNormal = FPhysXHelper::ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;

	if (FBodyInstance* HitBody = FPhysXHelper::GetBodyInstanceFromPxShape(Block.shape))
	{
		OutHit.HitComponent = HitBody->GetOwnerComponent();
		OutHit.HitActor = HitBody->GetOwnerActor();
	}
	else
	{
		OutHit.HitComponent = FPhysXHelper::GetOwnerComponentFromPxActor(Block.actor);
		OutHit.HitActor = FPhysXHelper::GetOwnerActorFromPxActor(Block.actor);
	}

	return true;
}

bool FPhysXPhysicsScene::RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	uint32 ObjectTypeMask, const AActor* IgnoreActor) const
{
	if (!Scene || ObjectTypeMask == 0) return false;

	// SetupFilterData (line ~322) 에서 word0 = ObjectType (채널 enum 값) 으로 set.
	// ObjectType 마스크 비트 검사로 hit 후보 필터.
	// Trigger flag shape 는 PhysX 측 query 단계에서 자동 제외.
	struct FObjectTypeRaycastFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 ObjectTypeMask = 0;

		FObjectTypeRaycastFilter(const AActor* InIgnoreActor, PxU32 InMask)
			: IgnoreActor(InIgnoreActor)
			, ObjectTypeMask(InMask)
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IgnoreActor && FPhysXHelper::GetOwnerActorFromPxActor(Actor) == IgnoreActor)
			{
				return PxQueryHitType::eNONE;
			}
			if (Shape)
			{
				const PxFilterData ShapeData = Shape->getQueryFilterData();
				const PxU32 ShapeObjectBit = 1u << ShapeData.word0;
				if ((ShapeObjectBit & ObjectTypeMask) == 0)
				{
					return PxQueryHitType::eNONE;
				}
			}
			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxRaycastBuffer Hit;
	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FObjectTypeRaycastFilter FilterCallback(IgnoreActor, ObjectTypeMask);

	bool bStatus = Scene->raycast(FPhysXHelper::ToPxVec3(Start), FPhysXHelper::ToPxVec3(Dir), MaxDist, Hit, PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxRaycastHit& Block = Hit.block;
	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = FPhysXHelper::ToFVector(Block.position);
	OutHit.ImpactNormal = FPhysXHelper::ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;

	if (FBodyInstance* HitBody = FPhysXHelper::GetBodyInstanceFromPxShape(Block.shape))
	{
		OutHit.HitComponent = HitBody->GetOwnerComponent();
		OutHit.HitActor = HitBody->GetOwnerActor();
	}
	else
	{
		OutHit.HitComponent = FPhysXHelper::GetOwnerComponentFromPxActor(Block.actor);
		OutHit.HitActor = FPhysXHelper::GetOwnerActorFromPxActor(Block.actor);
	}

	return true;
}

bool FPhysXPhysicsScene::SphereSweepShapeComponents(const FVector& Start, const FVector& Dir, float MaxDist, float Radius,
	FHitResult& OutHit, ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	if (!Scene || MaxDist <= 0.0f) return false;

	struct FShapeChannelSweepFilter : PxQueryFilterCallback
	{
		const AActor* IgnoreActor = nullptr;
		PxU32 TraceBit = 0;

		FShapeChannelSweepFilter(const AActor* InIgnoreActor, ECollisionChannel InChannel)
			: IgnoreActor(InIgnoreActor)
			, TraceBit(1u << static_cast<PxU32>(InChannel))
		{
		}

		PxQueryHitType::Enum preFilter(const PxFilterData&, const PxShape* Shape, const PxRigidActor* Actor, PxHitFlags&) override
		{
			if (IgnoreActor && FPhysXHelper::GetOwnerActorFromPxActor(Actor) == IgnoreActor)
			{
				return PxQueryHitType::eNONE;
			}

			FBodyInstance* Body = FPhysXHelper::GetBodyInstanceFromPxShape(Shape);
			UPrimitiveComponent* Comp = Body ? Body->GetOwnerComponent() : nullptr;
			if (!Comp || !Cast<UShapeComponent>(Comp))
			{
				return PxQueryHitType::eNONE;
			}

			const PxFilterData ShapeData = Shape->getQueryFilterData();
			if ((ShapeData.word1 & TraceBit) == 0)
			{
				return PxQueryHitType::eNONE;
			}

			return PxQueryHitType::eBLOCK;
		}

		PxQueryHitType::Enum postFilter(const PxFilterData&, const PxQueryHit&) override
		{
			return PxQueryHitType::eBLOCK;
		}
	};

	PxQueryFilterData FilterData;
	FilterData.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	FShapeChannelSweepFilter FilterCallback(IgnoreActor, TraceChannel);

	if (Radius <= 0.0f)
	{
		PxRaycastBuffer RayHit;
		const bool bStatus = Scene->raycast(FPhysXHelper::ToPxVec3(Start), FPhysXHelper::ToPxVec3(Dir), MaxDist, RayHit,
			PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
		if (!bStatus || !RayHit.hasBlock) return false;

		const PxRaycastHit& Block = RayHit.block;
		OutHit.bHit = true;
		OutHit.Distance = Block.distance;
		OutHit.WorldHitLocation = Start + Dir * Block.distance;
		OutHit.ImpactNormal = FPhysXHelper::ToFVector(Block.normal);
		OutHit.WorldNormal = OutHit.ImpactNormal;

		if (FBodyInstance* HitBody = FPhysXHelper::GetBodyInstanceFromPxShape(Block.shape))
		{
			OutHit.HitComponent = HitBody->GetOwnerComponent();
			OutHit.HitActor = HitBody->GetOwnerActor();
		}
		else
		{
			OutHit.HitComponent = FPhysXHelper::GetOwnerComponentFromPxActor(Block.actor);
			OutHit.HitActor = FPhysXHelper::GetOwnerActorFromPxActor(Block.actor);
		}

		return true;
	}

	PxSweepBuffer Hit;
	const PxSphereGeometry SweepGeometry(Radius);
	const PxTransform StartPose(FPhysXHelper::ToPxVec3(Start));
	const bool bStatus = Scene->sweep(SweepGeometry, StartPose, FPhysXHelper::ToPxVec3(Dir), MaxDist, Hit,
		PxHitFlag::eDEFAULT, FilterData, &FilterCallback);
	if (!bStatus || !Hit.hasBlock) return false;

	const PxSweepHit& Block = Hit.block;
	OutHit.bHit = true;
	OutHit.Distance = Block.distance;
	OutHit.WorldHitLocation = Start + Dir * Block.distance;
	OutHit.ImpactNormal = FPhysXHelper::ToFVector(Block.normal);
	OutHit.WorldNormal = OutHit.ImpactNormal;

	if (FBodyInstance* HitBody = FPhysXHelper::GetBodyInstanceFromPxShape(Block.shape))
	{
		OutHit.HitComponent = HitBody->GetOwnerComponent();
		OutHit.HitActor = HitBody->GetOwnerActor();
	}
	else
	{
		OutHit.HitComponent = FPhysXHelper::GetOwnerComponentFromPxActor(Block.actor);
		OutHit.HitActor = FPhysXHelper::GetOwnerActorFromPxActor(Block.actor);
	}

	return true;
}

// --- Body Instance ---
FBodyInstance* FPhysXPhysicsScene::GetBodyInstance(UPrimitiveComponent* Comp)
{
	if (!Comp || !FindMappingByComponent(Comp))
	{
		return nullptr;
	}

	FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	return BodyInstance && BodyInstance->IsValidBodyInstance() ? BodyInstance : nullptr;
}

const FBodyInstance* FPhysXPhysicsScene::GetBodyInstance(UPrimitiveComponent* Comp) const
{
	if (!Comp || !FindMappingByComponent(Comp))
	{
		return nullptr;
	}

	const FBodyInstance* BodyInstance = Comp->GetBodyInstance();
	return BodyInstance && BodyInstance->IsValidBodyInstance() ? BodyInstance : nullptr;
}

// ================================================================
// Constraint Section
// - Constraint Helpers
// - CreateConstraint
// - DestroyConstraint
// ================================================================

// --- Constraint Helper Section ---
static PxD6Motion::Enum ToPxD6Motion(ELinearConstraintMotion Motion)
{
	switch (Motion)
	{
	case ELinearConstraintMotion::Free:		return PxD6Motion::eFREE;
	case ELinearConstraintMotion::Limited:	return PxD6Motion::eLIMITED;
	case ELinearConstraintMotion::Locked:	
	default:								return PxD6Motion::eLOCKED;
	}
}

static PxD6Motion::Enum ToPxD6Motion(EAngularConstraintMotion Motion)
{
	switch (Motion)
	{
	case EAngularConstraintMotion::Free:	return PxD6Motion::eFREE;
	case EAngularConstraintMotion::Limited:	return PxD6Motion::eLIMITED;
	case EAngularConstraintMotion::Locked:
	default:								return PxD6Motion::eLOCKED;
	}
}

static bool IsAnyLinearMotionLimited(const FConstraintOption& Option)
{
	return Option.XMotion == ELinearConstraintMotion::Limited
		|| Option.YMotion == ELinearConstraintMotion::Limited
		|| Option.ZMotion == ELinearConstraintMotion::Limited;
}

static bool IsAnySwingMotionLimited(const FConstraintOption& Option)
{
	return Option.Swing1Motion == EAngularConstraintMotion::Limited
		|| Option.Swing2Motion == EAngularConstraintMotion::Limited;
}

static bool IsAnyAngularMotionFreeOrLimited(const FConstraintOption& Option)
{
	return Option.TwistMotion	!= EAngularConstraintMotion::Locked
		|| Option.Swing1Motion	!= EAngularConstraintMotion::Locked
		|| Option.Swing2Motion	!= EAngularConstraintMotion::Locked;
}

static void ApplyContraintOptionToD6Joint(PxD6Joint* Joint,
	const FConstraintOption& Option, const PxTolerancesScale& Scale)
{
	if (!Joint) return;

	// --- Linear DOF ---
	Joint->setMotion(PxD6Axis::eX, ToPxD6Motion(Option.XMotion));
	Joint->setMotion(PxD6Axis::eY, ToPxD6Motion(Option.YMotion));
	Joint->setMotion(PxD6Axis::eZ, ToPxD6Motion(Option.ZMotion));

	if (IsAnyLinearMotionLimited(Option))
	{
		const float LinearLimit = FMath::ClampMin(Option.LinearLimit, 0.0f);

		// Physx D6의 Linear Limit은 Limited Linear Axis 전체에 공통 적용
		Joint->setLinearLimit(PxJointLinearLimit(Scale, LinearLimit));
	}

	// --- Angular DOF ---
	Joint->setMotion(PxD6Axis::eTWIST,	ToPxD6Motion(Option.TwistMotion));
	Joint->setMotion(PxD6Axis::eSWING1, ToPxD6Motion(Option.Swing1Motion));
	Joint->setMotion(PxD6Axis::eSWING2, ToPxD6Motion(Option.Swing2Motion));

	if (Option.TwistMotion == EAngularConstraintMotion::Limited)
	{
		// PhysX Angular Limit: radian -> 0도 limited는 solver 입장에서 불안정 -> 작은 양수로 보정
		const float TwistLimitRad = FMath::ClampMin(Option.TwistLimitDegrees * FMath::DegToRad, FMath::DegToRad * 0.1f);
		Joint->setTwistLimit(PxJointAngularLimitPair(-TwistLimitRad, TwistLimitRad));
	}

	if (IsAnySwingMotionLimited(Option))
	{
		const float Swing1Rad = FMath::ClampMin(Option.Swing1LimitDegrees * FMath::DegToRad, 0.1f * FMath::DegToRad);
		const float Swing2Rad = FMath::ClampMin(Option.Swing2LimitDegrees * FMath::DegToRad, 0.1f * FMath::DegToRad);

		// Swing1 / Swing2는 Cone Limit으로 묶어서 적용
		Joint->setSwingLimit(PxJointLimitCone(Swing1Rad, Swing2Rad));
	}

	// --- Projection ---
	Joint->setConstraintFlag(PxConstraintFlag::ePROJECTION, Option.bEnableProjection);
	if (Option.bEnableProjection)
	{
		Joint->setProjectionLinearTolerance(FMath::ClampMin(Option.ProjectionLinearTolerance, 0.0f));
		Joint->setProjectionAngularTolerance(FMath::ClampMin(Option.ProjectionAngularToleranceDegrees * FMath::DegToRad, 0.0f));
	}

	// --- Angular Drive ---
	if (Option.bAngularDriveEnabled && IsAnyAngularMotionFreeOrLimited(Option))
	{
		const float ForceLimit = Option.AngularDriveForceLimit > 0.0f ? Option.AngularDriveForceLimit : PX_MAX_F32;
		const PxD6JointDrive Drive(
			FMath::ClampMin(Option.AngularDriveStiffness, 0.0f),
			FMath::ClampMin(Option.AngularDriveDamping, 0.0f),
			ForceLimit,
			false);

		// Slerp Drive
		Joint->setDrive(PxD6Drive::eSLERP, Drive);
		Joint->setDrivePosition(PxTransform(PxIdentity));
	}
}

FConstraintInstance* FPhysXPhysicsScene::CreateConstraint(FBodyInstance* Parent, FBodyInstance* Child, 
	const FConstraintOption& Option, const FTransform& ParentFrame, const FTransform& ChildFrame, const FString& ConstraintName /*= FString()*/)
{
	if (!Physics)
	{
		UE_LOG("[PhysX] CreateConstraint Failed : Physics is null");
		return nullptr;
	}

	if (!Parent || !Child)
	{
		UE_LOG("[PhysX] CreateConstraint Failed : Parent Or Child Body is Null");
		return nullptr;
	}

	PxRigidActor* ParentActor	= Parent->GetPxRigidActor();
	PxRigidActor* ChildActor	= Child->GetPxRigidActor();

	if (!ParentActor || !ChildActor)
	{
		UE_LOG("[PhysX] CreateConstraint Failed : PxRigidActor is null");
		return nullptr;
	}

	if (ParentActor == ChildActor)
	{
		UE_LOG("[PhysX] CreateConstraint Failed : Parent Actor == Child Actor");
		return nullptr;
	}

	// static-static joint 는 runtime constraint 의미가 없음
	if (!Parent->IsDynamic() && !Child->IsDynamic())
	{
		UE_LOG("[PhysX] CreateConstraint Failed : At Least One Body Must be dynamic");
		return nullptr;
	}

	auto NewConstraint = std::make_unique<FConstraintInstance>();

	NewConstraint->ConstraintName = ConstraintName;
	NewConstraint->ParentFrame = ParentFrame;
	NewConstraint->ChildFrame = ChildFrame;
	NewConstraint->Option = Option;
	NewConstraint->InitConstraint(Parent, Child);

	const PxTransform PxParentFrame = FPhysXHelper::ToPxTransform(ParentFrame);
	const PxTransform PxChildFrame = FPhysXHelper::ToPxTransform(ChildFrame);

	PxD6Joint* Joint = PxD6JointCreate(
		*Physics,
		ParentActor,
		PxParentFrame,
		ChildActor,
		PxChildFrame
	);

	if (!Joint)
	{
		UE_LOG("[PhysX] PxD6JointCreate failed");
		return nullptr;
	}

	ApplyContraintOptionToD6Joint(Joint, Option, Physics->getTolerancesScale());

	// Joint Relase는 FConstraintInstance::TerminateConstraint가 담당
	NewConstraint->SetConstraintHandle(Joint);

	FConstraintInstance* Result = NewConstraint.get();
	Constraints.push_back(std::move(NewConstraint));

	return Result;
}

void FPhysXPhysicsScene::DestroyConstraint(FConstraintInstance* Constraint)
{
	if (!Constraint) return;

	Constraint->TerminateConstraint();

	Constraints.erase(
		std::remove_if(
			Constraints.begin(),
			Constraints.end(),
			[Constraint](const std::unique_ptr<FConstraintInstance>& Ptr)
			{
				return Ptr.get() == Constraint;
			}
		),
		Constraints.end()
	);
}

/*
* Body가 제거될 때 해당 body를 참조하는 joint가 남으면, 
* PhysX actor release 이후 joint가 죽은 actor를 물고 있게 됩니다. 
* 
* 그래서 body release 전에 연결된 constraint를 먼저 지워야 합니다.
*/
void FPhysXPhysicsScene::DestroyConstraintsForBody(FBodyInstance* BodyInstance)
{
	if (!BodyInstance) return;

	Constraints.erase(std::remove_if(Constraints.begin(), Constraints.end(),
		[BodyInstance](const std::unique_ptr<FConstraintInstance>& Ptr)
		{
			if (!Ptr) return true;
			if (Ptr->ParentBody == BodyInstance || Ptr->ChildBody == BodyInstance)
			{
				Ptr->TerminateConstraint();
				return true;
			}
			return false;
		}),
		Constraints.end()
	);
}
