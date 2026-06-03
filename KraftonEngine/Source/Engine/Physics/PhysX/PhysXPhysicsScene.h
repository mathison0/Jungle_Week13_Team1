#pragma once

#include "Physics/IPhysicsScene.h"
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"

#include <memory>

class AActor;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UBodySetup;
struct FPhysXShapeDesc;

// Forward declarations — PhysX types
namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxScene;
	class PxDefaultCpuDispatcher;
	class PxMaterial;
	class PxRigidActor;
	class PxShape;

#ifdef _DEBUG
	// PVD(PhysX Visual Debugger) 관련 객체
	class PxPvd;
	class PxPvdTransport;
#endif
}

class FPhysXSimulationCallback;
class UPhysicalMaterial;
class FPhysXVehicle4W;
class FPhysXRockerBogieVehicle;

// ============================================================
// FPhysXPhysicsScene — PhysX 4.1 기반 물리 시스템
//
// IPhysicsScene 인터페이스를 통해 Native와 교체 가능.
//
// 컴포넌트가 자기 FBodyInstance를 소유하고, Scene은 대표 body들의 비소유
// 레지스트리(Bodies)로 매 프레임 순회만 한다. ragdoll body/constraint는
// SkeletalMeshComponent가 소유한다.
//
// 같은 액터의 여러 PrimitiveComponent는 하나의 PxRigidActor에 shape로 합쳐지고
// (LocalPose는 대표 컴포넌트 기준), 차체 Box + 바퀴 Sphere처럼 다중 콜라이더가
// 한 강체로 동작한다.
// ============================================================
class FPhysXPhysicsScene : public IPhysicsScene
{
public:
	void Initialize(UWorld* InWorld) override;
	void Shutdown() override;

	void RegisterComponent(UPrimitiveComponent* Comp) override;
	void UnregisterComponent(UPrimitiveComponent* Comp) override;
	void RebuildBody(UPrimitiveComponent* Comp) override;
	bool InstantiatePhysicsAssetBodies(USkeletalMeshComponent* Comp) override;
	void DestroyPhysicsAssetBodies(USkeletalMeshComponent* Comp) override;
	bool SyncPhysicsAssetBodiesToComponentPose(USkeletalMeshComponent* Comp, bool bResetVelocity = true) override;
	void SetPhysicsAssetBodiesSimulate(USkeletalMeshComponent* Comp, bool bSimulate) override;
	void GatherClothCollision(const FClothCollisionGatherDesc& Desc, FClothCollisionData& OutData) const override;

	void Tick(float DeltaTime) override;
	FPhysicsSceneStats GetStats() const override { return LastStats; }

	// --- Force, Torque ----
	void AddForce(UPrimitiveComponent* Comp, const FVector& Force) override;
	void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) override;
	void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) override;
	void AddImpulse(UPrimitiveComponent* Comp, const FVector& Impulse) override;
	void AddImpulseAtLocation(UPrimitiveComponent* Comp, const FVector& Impulse, const FVector& WorldLocation) override;
	void AddAngularImpulse(UPrimitiveComponent* Comp, const FVector& AngularImpulse) override;

	// --- Velocity (선속도, 각속도) ---
	FVector GetLinearVelocity(UPrimitiveComponent* Comp) const override;
	void SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;
	FVector GetAngularVelocity(UPrimitiveComponent* Comp) const override;
	void SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel) override;
	
	// ---  Mass ---
	void SetMass(UPrimitiveComponent* Comp, float Mass) override;
	float GetMass(UPrimitiveComponent* Comp) const override;
	void SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset) override;
	FVector GetCenterOfMass(UPrimitiveComponent* Comp) const override;

	// --- Ray Section ---
	bool Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const override;

	bool RaycastByObjectTypes(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
		uint32 ObjectTypeMask, const AActor* IgnoreActor = nullptr) const override;

	bool SphereSweepShapeComponents(const FVector& Start, const FVector& Dir, float MaxDist, float Radius,
		FHitResult& OutHit,
		ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic,
		const AActor* IgnoreActor = nullptr) const override;

	// --- Body Instance ---
	// PhysX 전용 Body Lookup
	// Constraint 생성자는 Body Instance 요구 -> 외부에서 Component->BodyInstance 획득
	FBodyInstance* GetBodyInstance(UPrimitiveComponent* Comp);
	const FBodyInstance* GetBodyInstance(UPrimitiveComponent* Comp) const;

	// --- Constraint Instance ---
	// joint를 만들어 소유권(unique_ptr)을 호출자(컴포넌트)에게 넘긴다.
	// DestroyConstraint는 PxJoint만 해제하며 FConstraintInstance 객체는 호출자가 소유·삭제한다.
	std::unique_ptr<FConstraintInstance> CreateConstraint(FBodyInstance* Parent, FBodyInstance* Child,
		const FConstraintSetup& Setup);

	void DestroyConstraint(FConstraintInstance* Constraint);

	// --- PhysicsAsset / Ragdoll Adapter ---
	// PhysicsAsset/Ragdoll 빌더가 사용할 body 생성/해제 진입점.
	// BodySetup 1개를 world transform 위치에 독립 body로 만들어 소유권(unique_ptr)을 호출자에게 넘긴다.
	// 호출자(컴포넌트)가 반환 body를 보관하고, joint는 CreateConstraint로, PhysX 자원 해제는 DestroyBody로 한다.
	// (DestroyBody는 PxRigidActor만 해제하며 FBodyInstance 객체는 호출자가 소유·삭제한다.)
	std::unique_ptr<FBodyInstance> CreateBodyFromBodySetup(UPrimitiveComponent* OwnerComp, UBodySetup* BodySetup,
		const FTransform& WorldTransform, bool bDynamic, float UniformScale = 1.0f);
	void DestroyBody(FBodyInstance* Body);

	// --- Vehicle / 고급 PhysX 접근 게이트 ---
	// PxVehicle 등 PhysX를 직접 제어해야 하는 코드용. 호출 측은 IPhysicsScene을
	// FPhysXPhysicsScene으로 다운캐스트 + 백엔드 가드(Backend == PhysX) 후 사용한다.
	// (FBodyInstance의 PhysX 핸들은 계속 private — Scene이 중개한다.)
	physx::PxScene* GetPxScene() const { return Scene; }
	physx::PxRigidActor* GetComponentRigidActor(UPrimitiveComponent* Comp);

	// PxVehicle 빌드에 필요한 공유 핸들. (PhysX 백엔드에서만 유효)
	physx::PxPhysics* GetPhysics() const { return Physics; }
	physx::PxMaterial* GetDefaultMaterial() const { return DefaultMaterial; }

	// 차량 1대 등록/해제. 등록된 차는 매 프레임 simulate() 직전에 Simulate()가 불린다.
	// 차량 객체 소유권은 컴포넌트에 있고 여기엔 포인터만 둔다(비소유).
	void RegisterVehicle(FPhysXVehicle4W* Vehicle) { ActiveVehicle = Vehicle; }
	void UnregisterVehicle(FPhysXVehicle4W* Vehicle) { if (ActiveVehicle == Vehicle) ActiveVehicle = nullptr; }
	void RegisterRockerBogieVehicle(FPhysXRockerBogieVehicle* Vehicle) { ActiveRockerBogieVehicle = Vehicle; }
	void UnregisterRockerBogieVehicle(FPhysXRockerBogieVehicle* Vehicle) { if (ActiveRockerBogieVehicle == Vehicle) ActiveRockerBogieVehicle = nullptr; }

private:
	UWorld* World = nullptr;

	// PhysX core objects
	physx::PxFoundation* Foundation = nullptr;
	physx::PxPhysics* Physics = nullptr;
	physx::PxScene* Scene = nullptr;
	physx::PxDefaultCpuDispatcher* Dispatcher = nullptr;

	// DefaultMaterialOverride가 생성한 PxMaterial Cache (직접 ReleaseX)
	physx::PxMaterial* DefaultMaterial = nullptr;
	UPhysicalMaterial* DefaultPhysicalMaterial = nullptr;

	FPhysXSimulationCallback* EventCallback = nullptr;

	// 현재 씬에서 구동 중인 차량(비소유). Tick의 simulate() 직전에 Simulate()를 호출.
	FPhysXVehicle4W* ActiveVehicle = nullptr;
	FPhysXRockerBogieVehicle* ActiveRockerBogieVehicle = nullptr;

	// 랙돌/동적 바디는 렌더 FPS 변화에 민감하므로 fixed timestep으로 적분한다.
	// frame 시간을 누적했다가 60Hz 단위로만 simulate하고, 너무 큰 누적 시간은 버려 발산을 막는다.
	float PhysicsTimeAccumulator = 0.0f;

#ifdef _DEBUG
	// PVD는 전역 PhysX 객체와 같이 공유
	// Scene 단위 소유가 아니기 때문에 관찰용 포인터만 보관
	physx::PxPvd* Pvd = nullptr;
	physx::PxPvdTransport* PvdTransport = nullptr;
#endif

	// 살아있는 강체들의 대표 body 목록. 객체 소유는 컴포넌트가 하고 여기엔 포인터만 둔다(매 프레임 순회용).
	// 한 강체에 여러 컴포넌트가 합쳐져도 대표 하나만 여기 들어간다.
	TArray<FBodyInstance*> Bodies;

	// ragdoll body/constraint는 컴포넌트(SkeletalMeshComponent)가 소유한다. 여기엔
	// bone 계층 writeback(SyncPhysicsAssetBodiesToBones)을 위해 컴포넌트 목록만 둔다.
	TArray<USkeletalMeshComponent*> SkeletalPhysicsComponents;
	FPhysicsSceneStats LastStats;

	// 내부 헬퍼
	// Comp가 속한 강체의 대표 body. 등록 안 됐으면 nullptr.
	FBodyInstance* FindHostBody(UPrimitiveComponent* Comp);
	const FBodyInstance* FindHostBody(UPrimitiveComponent* Comp) const;
	// Actor의 강체 대표 body. ragdoll/adapter body는 제외.
	FBodyInstance* FindHostBodyByActor(AActor* OwnerActor);
	// 강체 하나의 PhysX 자원(PxRigidActor)을 해제하는 공통 경로. FBodyInstance 객체는 소유자가 지우므로 여기서 delete하지 않는다.
	void ReleaseBodyResource(FBodyInstance* Body);
	void SyncPhysicsAssetBodiesToBones(float DeltaTime);
	// 위 함수의 반대 방향. ragdoll이 꺼진 동안 kinematic body를 매 frame animation bone pose로 끌고 간다.
	// 없으면 BeginPlay에서 생성된 kinematic body가 첫 pose에 얼어붙어 raycast/overlap이 헛맞는다.
	void SyncKinematicPhysicsAssetBodiesToBones();

	// FPhysXShapeDesc 하나를 주어진 actor에 PxShape로 생성. 실패 시 nullptr.
	physx::PxShape* CreateShapeOnActor(physx::PxRigidActor* Actor, const FPhysXShapeDesc& Desc);

	// HostActor에 Comp의 geometry를 shape로 추가(RootComp 기준 LocalPose). 실패 시 nullptr.
	physx::PxShape* AddShapeForComponent(physx::PxRigidActor* HostActor, UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp);
	// HostActor에 Comp의 BodySetup AggGeom을 shape로 추가. shape가 하나 이상 생성되면 true.
	bool AddShapesFromBodySetup(physx::PxRigidActor* HostActor, UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp);
	// StaticMeshComponent 전용 triangle mesh 경로.
	// StaticMesh package에 저장된 cooked data를 PxTriangleMesh simulation/query shape로 붙인다.
	// 런타임 등록 단계에서는 vertex/index 해시 계산이나 recook을 하지 않는다.
	bool AddTriangleMeshShapeFromStaticMesh(physx::PxRigidActor* HostActor, UPrimitiveComponent* RootComp, UStaticMeshComponent* Comp);
	// HostActor에서 Comp에 매칭된 shape를 detach.
	void DetachShapeForComponent(physx::PxRigidActor* HostActor, UPrimitiveComponent* Comp);

	mutable uint32 PendingRaycastQueries = 0;
	mutable uint32 PendingSweepQueries = 0;
};
