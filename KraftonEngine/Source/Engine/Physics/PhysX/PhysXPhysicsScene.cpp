#include "PhysXPhysicsScene.h"
#include "PhysXCore.h"
#include "PhysXCollision.h"
#include "PhysXSimulationCallback.h"
#include "PhysXHelper.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Math/Quat.h"
#include "Object/Object.h"
#include "Core/Logging/Log.h"
#include "Physics/PhysicalMaterial.h"

// PhysX headers
#include <PxPhysicsAPI.h>

#include <algorithm>
#include <memory>

using namespace physx;

// Dispatcher Thread 수
// TODO: 프로젝트 세팅으로 빼서 개수 조절할 수 있게 만들기 고려
static constexpr int32 GPhysXWorkerThreadCount = 2;

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
// ============================================================
// Lifecycle
// ============================================================

void FPhysXPhysicsScene::Initialize(UWorld* InWorld)
{
	World = InWorld;

	// Foundation / Physics — 프로세스 싱글턴 공유
#ifdef _DEBUG
	if (!FPhysXCore::Acquire(Foundation, Physics, Pvd, PvdTransport))
#else
	if (!FPhysXCore::Acquire(Foundation, Physics))
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
	SceneDesc.filterShader = FPhysXCollision::FilterShader;
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
	while (!SkeletalPhysicsComponents.empty())
	{
		DestroyPhysicsAssetBodies(SkeletalPhysicsComponents.back());
	}

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

	// Adapter로 생성한 독립 body 정리. 위에서 constraint를 먼저 끊었으므로 actor release만 한다.
	for (auto& BodyPtr : AdapterBodies)
	{
		if (!BodyPtr) continue;
		if (physx::PxRigidActor* Actor = FPhysXHelper::GetRigidActor(BodyPtr.get()))
		{
			FPhysXHelper::SetActorBodyRecord(Actor, nullptr);
			if (Scene) Scene->removeActor(*Actor);
			BodyPtr->TerminateBody();
			Actor->release();
		}
	}
	AdapterBodies.clear();

	// Body 정리
	for (auto& MappingPtr : BodyMappings)
	{
		if (!MappingPtr) continue;
		FBodyMapping& Mapping = *MappingPtr;

		if (Mapping.Actor)
		{
			FPhysXHelper::SetActorBodyRecord(Mapping.Actor, nullptr);

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
	FPhysXCore::Release();

	World = nullptr;

	UE_LOG("[PhysX] Scene shutdown complete.");
}

// ============================================================
// Body 관리 — Actor 단위 compound
//
// 한 액터의 여러 PrimitiveComponent는 같은 PxRigidActor에 shape로 합쳐진다.
// shape의 LocalPose는 액터 RootComponent에 대한 상대 transform.
// userData: PxActor -> 대표 FBodyInstance, PxShape -> shape owner FBodyInstance.
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

		FPhysXHelper::SetActorBodyRecord(Body, RootPrim->GetBodyInstance());

		Scene->addActor(*Body);

		BodyMappings.push_back(std::move(NewMapping));
		Mapping = BodyMappings.back().get();
	}

	// shape 추가 — BodySetup이 있으면 AggGeom 경로, 없으면 ShapeComponent 경로
	bool bShapeAdded;
	if (Comp->GetBodySetup())
	{
		bShapeAdded = AddShapesFromBodySetup(*Mapping, Comp);
	}
	else
	{
		bShapeAdded = (AddShapeForComponent(*Mapping, Comp) != nullptr);
	}
	if (!bShapeAdded) return;
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
			FPhysXHelper::SetActorBodyRecord(Mapping->Actor, nullptr);

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
		FPhysXHelper::SetActorBodyRecord(Mapping->Actor, Mapping->RootComp ? Mapping->RootComp->GetBodyInstance() : nullptr);
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

	SyncPhysicsAssetBodiesToBones();

	// ── Dispatch deferred contact/trigger events ──
	// onContact / onTrigger 는 fetchResults 안에서 fire 되므로 거기서 직접 게임 핸들러를
	// 부르면 핸들러의 World->DestroyActor 등이 PhysX scene 변경 타이밍과 겹쳐 크래쉬한다.
	// 그래서 큐에만 적재했고, 이 시점(simulate/fetchResults 외부)에서 한꺼번에 dispatch.
	if (EventCallback)
	{
		EventCallback->DispatchPendingEvents();
	}
}
