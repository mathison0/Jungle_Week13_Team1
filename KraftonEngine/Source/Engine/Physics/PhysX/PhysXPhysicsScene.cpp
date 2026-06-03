#include "PhysXPhysicsScene.h"
#include "PhysXCore.h"
#include "PhysXCollision.h"
#include "PhysXSimulationCallback.h"
#include "PhysXClothCollisionReader.h"
#include "PhysXHelper.h"
#include "Physics/PhysX/Vehicle/PhysXVehicle4W.h"
#include "Physics/PhysX/Vehicle/PhysXRockerBogieVehicle.h"
#include "Component/Primitive/ClothComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Math/Quat.h"
#include "Object/Object.h"
#include "Core/Logging/Log.h"
#include "Physics/PhysicsMaterial/PhysicalMaterial.h"

// PhysX headers
#include <PxPhysicsAPI.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

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
	PhysicsTimeAccumulator = 0.0f;

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

	// (ragdoll body/constraint는 위 SkeletalPhysicsComponents drain에서 컴포넌트별로 이미 정리됨)

	// 대표 body들 정리 — ReleaseBodyResource가 같은 강체에 합쳐진 컴포넌트들의 body까지 정리한다.
	for (FBodyInstance* Host : Bodies)
	{
		ReleaseBodyResource(Host);
	}
	Bodies.clear();

	// 차량은 컴포넌트가 소유·해제하므로 여기선 포인터만 끊는다(컴포넌트 EndPlay에서 Release 가정).
	ActiveVehicle = nullptr;
	ActiveRockerBogieVehicle = nullptr;

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
	PhysicsTimeAccumulator = 0.0f;

	UE_LOG("[PhysX] Scene shutdown complete.");
}

// ============================================================
// Body 관리 — 컴포넌트 등록, 같은 액터는 한 강체로 weld
//
// 같은 액터의 여러 PrimitiveComponent는 한 PxRigidActor에 shape로 합쳐진다.
// shape의 LocalPose는 대표 컴포넌트(OwnerComponent) 기준 상대 transform.
// userData: PxActor -> 대표 FBodyInstance, PxShape -> shape owner FBodyInstance.
// ============================================================

void FPhysXPhysicsScene::RegisterComponent(UPrimitiveComponent* Comp)
{
	if (!Comp || !Scene || !Physics || !DefaultMaterial) return;
	if (FindHostBody(Comp)) return; // 이미 등록됨

	AActor* OwnerActor = Comp->GetOwner();
	if (!OwnerActor) return;

	FBodyInstance* Host = FindHostBodyByActor(OwnerActor);

	if (!Host)
	{
		UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent());
		if (!RootPrim) RootPrim = Comp;

		const bool bDynamic = RootPrim->GetSimulatePhysics();
		PxTransform BodyXf = FPhysXHelper::ToPxTransform(RootPrim);

		PxRigidActor* Body = bDynamic
			? static_cast<PxRigidActor*>(Physics->createRigidDynamic(BodyXf))
			: static_cast<PxRigidActor*>(Physics->createRigidStatic(BodyXf));
		if (!Body) return;

		// 강체의 대표 body는 RootComponent의 BodyInstance가 맡는다.
		// 같은 액터의 다른 컴포넌트들은 이 강체에 충돌 모양만 얹어 공유한다.
		Host = RootPrim->GetBodyInstance();
		Host->InitBody(RootPrim, Body);
		Host->CombinedComponents.clear();

		FPhysXHelper::SetActorBodyRecord(Body, Host);

		Scene->addActor(*Body);

		Bodies.push_back(Host);
	}

	PxRigidActor* HostActor = FPhysXHelper::GetRigidActor(Host);

	// shape 추가 — BodySetup이 있으면 AggGeom 경로, 없으면 ShapeComponent 경로
	bool bShapeAdded;
	if (Comp->GetBodySetup())
	{
		bShapeAdded = AddShapesFromBodySetup(HostActor, Host->GetOwnerComponent(), Comp);
	}
	else
	{
		bShapeAdded = (AddShapeForComponent(HostActor, Host->GetOwnerComponent(), Comp) != nullptr);
	}
	if (!bShapeAdded) return;
	Comp->GetBodyInstance()->InitBody(Comp, HostActor);
	Host->CombinedComponents.push_back(Comp);

	// Dynamic이면 RootComp의 Mass / CenterOfMass로 갱신 (shape 추가될 때마다 inertia 재계산).
	if (PxRigidDynamic* Dyn = HostActor->is<PxRigidDynamic>())
	{
		ApplyRootMassAndCOM(Dyn, Host->GetOwnerComponent());
	}
}
void FPhysXPhysicsScene::UnregisterComponent(UPrimitiveComponent* Comp)
{
	if (!Comp || !Scene) return;

	FBodyInstance* Host = FindHostBody(Comp);
	if (!Host) return;

	FBodyInstance* ComponentBody = Comp->GetBodyInstance();

	PxRigidActor* HostActor = FPhysXHelper::GetRigidActor(Host);

	// 해당 컴포넌트의 shape detach
	DetachShapeForComponent(HostActor, Comp);

	// 이 강체의 컴포넌트 목록에서 제거
	Host->CombinedComponents.erase(
		std::remove(Host->CombinedComponents.begin(), Host->CombinedComponents.end(), Comp),
		Host->CombinedComponents.end());

	// 마지막 컴포넌트가 빠지면 강체 자체도 release
	if (Host->CombinedComponents.empty())
	{
		// 떠나는 컴포넌트의 body가 대표와 다르면 따로 정리 (대표는 ReleaseBodyResource가 처리).
		if (ComponentBody && ComponentBody != Host)
		{
			ComponentBody->TerminateBody();
		}
		ReleaseBodyResource(Host);
		Bodies.erase(std::remove(Bodies.begin(), Bodies.end(), Host), Bodies.end());
		return;
	}

	if (Comp == Host->GetOwnerComponent())
	{
		// 대표 컴포넌트가 빠지면, 남은 컴포넌트 중 하나가 강체의 새 대표가 된다.
		// 강체를 대표 body가 들고 있으므로 대표 자리를 통째로 넘겨준다.
		UPrimitiveComponent* NewRoot = Host->CombinedComponents.front();
		FBodyInstance* NewHost = NewRoot->GetBodyInstance();

		NewHost->InitBody(NewRoot, HostActor);
		NewHost->CombinedComponents = Host->CombinedComponents;

		FPhysXHelper::SetActorBodyRecord(HostActor, NewHost);
		std::replace(Bodies.begin(), Bodies.end(), Host, NewHost);

		Host->TerminateBody(); // 옛 대표 body. 강체는 새 대표가 이어받음
		Host = NewHost;
	}
	else if (ComponentBody && ComponentBody != Host)
	{
		ComponentBody->TerminateBody();
	}

	// 남은 shape가 있으면 mass/inertia 재계산
	if (PxRigidDynamic* Dyn = HostActor->is<PxRigidDynamic>())
	{
		ApplyRootMassAndCOM(Dyn, Host->GetOwnerComponent());
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

	FBodyInstance* Host = FindHostBodyByActor(OwnerActor);
	if (!Host) return; // 등록 안 됨 — skip

	// 같은 액터의 모든 컴포넌트를 미리 복사 (unregister 도중 대표 body가 사라질 수 있으므로)
	TArray<UPrimitiveComponent*> CompList = Host->CombinedComponents;

	for (UPrimitiveComponent* C : CompList)
	{
		UnregisterComponent(C);
	}
	for (UPrimitiveComponent* C : CompList)
	{
		RegisterComponent(C);
	}
}

void FPhysXPhysicsScene::GatherClothCollision(const FClothCollisionGatherDesc& Desc, FClothCollisionData& OutData) const
{
	OutData.Reset();
	if (!Scene || !Desc.ClothComponent)
	{
		return;
	}

	const FMatrix& ClothWorldInverse = Desc.ClothComponent->GetWorldInverseMatrix();
	std::vector<PxRigidActor*> VisitedActors;

	auto GatherRigidActor = [&](PxRigidActor* Actor)
	{
		if (!Actor)
		{
			return;
		}

		if (std::find(VisitedActors.begin(), VisitedActors.end(), Actor) != VisitedActors.end())
		{
			return;
		}
		VisitedActors.push_back(Actor);

		const PxU32 NumShapes = Actor->getNbShapes();
		if (NumShapes == 0)
		{
			return;
		}

		std::vector<PxShape*> Shapes(NumShapes);
		Actor->getShapes(Shapes.data(), NumShapes);

		for (PxShape* Shape : Shapes)
		{
			UPrimitiveComponent* OwnerComponent = Shape ? FPhysXHelper::GetOwnerComponentFromPxShape(Shape) : nullptr;

			if (!Shape || !Shape->getFlags().isSet(PxShapeFlag::eSIMULATION_SHAPE))
			{
				continue;
			}

			if (OwnerComponent == Desc.ClothComponent)
			{
				continue;
			}

			if (OwnerComponent && !OwnerComponent->IsCollisionEnabled())
			{
				continue;
			}

			FPhysXClothCollisionReader::AppendNvClothCollisionFromPxShape(Actor, Shape, Desc.CollisionThickness, ClothWorldInverse, OutData);
		}
	};

	for (FBodyInstance* Host : Bodies)
	{
		GatherRigidActor(FPhysXHelper::GetRigidActor(Host));
	}

	for (USkeletalMeshComponent* SkeletalComponent : SkeletalPhysicsComponents)
	{
		if (!SkeletalComponent)
		{
			continue;
		}

		for (const std::unique_ptr<FBodyInstance>& Body : SkeletalComponent->GetBodies())
		{
			GatherRigidActor(FPhysXHelper::GetRigidActor(Body.get()));
		}
	}
}

// ============================================================
// Simulation
// ============================================================

void FPhysXPhysicsScene::Tick(float DeltaTime)
{
	if (!Scene || DeltaTime <= 0.0f) return;

	// 랙돌/동적 바디 안정화:
	// 기존처럼 frame DeltaTime을 그대로 simulate()에 넣으면 같은 관절 오차/관통 오차라도
	// FPS에 따라 solver 보정 속도와 contact impulse가 달라진다. 특히 Release처럼 dt가 작을 때
	// 같은 위치 오차를 더 짧은 시간에 풀려고 하면서 랙돌이 위로 튀는 증상이 커질 수 있다.
	// 그래서 PhysX 적분은 60Hz fixed timestep으로만 진행해 Debug/Release의 물리 결과 차이를 줄인다.
	constexpr float FixedPhysicsDeltaTime = 1.0f / 60.0f;
	constexpr int32 MaxPhysicsSubsteps = 4;
	constexpr float MaxPhysicsDeltaTime = 0.1f;
	constexpr float MaxAccumulatedPhysicsTime = FixedPhysicsDeltaTime * MaxPhysicsSubsteps;

	// 큰 hitch가 들어와도 무한히 따라잡으려 하지 않는다.
	// frame dt는 기존 안전값(0.1s)으로 먼저 자르고, 누적 시간은 max substep까지만 보관한다.
	// 초과 시간은 버려 spiral of death와 한 프레임 다중 폭주를 막는다.
	if (DeltaTime > MaxPhysicsDeltaTime)
	{
		DeltaTime = MaxPhysicsDeltaTime;
	}

	PhysicsTimeAccumulator = std::min(
		PhysicsTimeAccumulator + DeltaTime,
		MaxAccumulatedPhysicsTime);

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

	for (FBodyInstance* Host : Bodies)
	{
		if (!Host || !Host->GetOwnerComponent()) continue;
		PxRigidActor* Actor = FPhysXHelper::GetRigidActor(Host);
		if (!Actor) continue;

		PxTransform NewPose = FPhysXHelper::ToPxTransform(Host->GetOwnerComponent());

		if (PxRigidDynamic* Dynamic = Actor->is<PxRigidDynamic>())
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
		else if (Actor->is<PxRigidStatic>())
		{
			Actor->setGlobalPose(NewPose);
		}
	}

	// ── Pre-simulate: 비-ragdoll skeletal body의 kinematic pose를 animation bone에 맞춘다 ──
	// ragdoll OFF인 컴포넌트의 body는 kinematic이며, 여기서 매 frame bone pose를 target으로 줘야
	// animation/캐릭터 이동을 따라온다. (ragdoll ON 컴포넌트는 post-simulate의
	// SyncPhysicsAssetBodiesToBones가 body→bone 반대 방향으로 처리하므로 여기선 건너뛴다.)
	SyncKinematicPhysicsAssetBodiesToBones();

	float SimulatedDeltaTime = 0.0f;
	int32 StepCount = 0;

	// ── Simulate ──
	// 통계 기능도 유지하기 위해 fixed substep 전체 실행 시간을 측정한다.
	// StepCount가 0이면 이번 Tick에는 누적 시간이 부족해 simulate를 돌리지 않은 상태다.
	const auto PhysicsStartTime = std::chrono::high_resolution_clock::now();
	while (PhysicsTimeAccumulator >= FixedPhysicsDeltaTime && StepCount < MaxPhysicsSubsteps)
	{
		// ── Vehicle: 입력 보간 + 서스펜션 raycast + 힘 적용 (반드시 simulate 직전) ──
		// PxVehicleUpdates의 dt는 바로 뒤따르는 simulate()의 dt와 같아야 한다(SDK 계약).
		// 그래서 fixed dt로, 씬과 같은 횟수만큼 substep 안에서 돌린다. frame dt로 한 번만
		// 돌리면 힘이 어긋나 차가 덜덜 떨리며 전진하지 못한다.
		if (ActiveVehicle)
		{
			ActiveVehicle->Simulate(FixedPhysicsDeltaTime);
		}
		if (ActiveRockerBogieVehicle)
		{
			ActiveRockerBogieVehicle->Simulate(FixedPhysicsDeltaTime);
		}

		// ── Simulate: 랙돌/동적 바디는 항상 고정 dt로 적분 ──
		Scene->simulate(FixedPhysicsDeltaTime);
		Scene->fetchResults(true);

		PhysicsTimeAccumulator -= FixedPhysicsDeltaTime;
		SimulatedDeltaTime += FixedPhysicsDeltaTime;
		++StepCount;
	}

	if (StepCount == MaxPhysicsSubsteps && PhysicsTimeAccumulator >= FixedPhysicsDeltaTime)
	{
		PhysicsTimeAccumulator = 0.0f;
	}
	const auto PhysicsEndTime = std::chrono::high_resolution_clock::now();

	PxSimulationStatistics SimulationStats;
	Scene->getSimulationStatistics(SimulationStats);
	LastStats.PhysicsTimeMs = StepCount > 0
		? std::chrono::duration<double, std::milli>(PhysicsEndTime - PhysicsStartTime).count()
		: 0.0;
	LastStats.RigidBodiesStatic = SimulationStats.nbStaticBodies;
	LastStats.RigidBodiesDynamic = SimulationStats.nbDynamicBodies;
	LastStats.RigidBodiesKinematic = SimulationStats.nbKinematicBodies;
	LastStats.RigidBodiesTotal =
		LastStats.RigidBodiesStatic +
		LastStats.RigidBodiesDynamic +
		LastStats.RigidBodiesKinematic;
	LastStats.RigidBodiesActive = SimulationStats.nbActiveDynamicBodies + SimulationStats.nbActiveKinematicBodies;
	LastStats.JointsCount = Scene->getNbConstraints();
	LastStats.ContactPairs = SimulationStats.nbDiscreteContactPairsTotal;
	LastStats.RaycastQueries = PendingRaycastQueries;
	LastStats.SweepQueries = PendingSweepQueries;
	PendingRaycastQueries = 0;
	PendingSweepQueries = 0;

	// ── Post-simulate: PhysX → Engine Transform 동기화 ──
	// RootComp에만 transform 적용 → 자식 컴포넌트는 attach로 자동 따라감.
	if (SimulatedDeltaTime > 0.0f)
	{
		for (FBodyInstance* Host : Bodies)
		{
			if (!Host || !Host->GetOwnerComponent()) continue;
			if (!Host->IsDynamic()) continue;
			if (Host->IsKinematic()) continue;
			if (Host->IsInstanceSleeping()) continue;

			FVector NewPos = Host->GetEngineWorldLocation();
			FQuat NewRot = Host->GetEngineWorldRotation();

			Host->GetOwnerComponent()->SetWorldLocation(NewPos);
			Host->GetOwnerComponent()->SetRelativeRotation(NewRot);
		}

		// 여러 substep을 돈 경우 실제로 적분한 물리 시간만큼 body→bone 동기화를 진행한다.
		// render DeltaTime을 그대로 넘기면 랙돌 블렌드/보정도 다시 FPS 영향을 받는다.
		SyncPhysicsAssetBodiesToBones(SimulatedDeltaTime);
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
