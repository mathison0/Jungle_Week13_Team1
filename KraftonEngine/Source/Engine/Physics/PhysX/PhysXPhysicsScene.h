#pragma once

#include "Physics/IPhysicsScene.h"
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"

#include <memory>

class AActor;
class USkeletalMeshComponent;
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

// ============================================================
// FPhysXPhysicsScene — PhysX 4.1 기반 물리 시스템
//
// IPhysicsScene 인터페이스를 통해 Native와 교체 가능.
//
// 등록 단위는 Actor — 한 액터의 여러 PrimitiveComponent는 하나의
// PxRigidActor에 compound shape로 합쳐진다. 각 shape의 LocalPose는
// 액터 RootComponent에 대한 상대 transform. 이로써 차체 Box + 바퀴
// Sphere 4개처럼 다중 콜라이더가 자연스럽게 한 강체로 동작한다.
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

	void Tick(float DeltaTime) override;

	// --- Force, Torque ----
	void AddForce(UPrimitiveComponent* Comp, const FVector& Force) override;
	void AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation) override;
	void AddTorque(UPrimitiveComponent* Comp, const FVector& Torque) override;

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
	FConstraintInstance* CreateConstraint(FBodyInstance* Parent, FBodyInstance* Child,
		const FConstraintOption& Option,
		const FTransform& ParentFrame,
		const FTransform& ChildFrame,
		const FString& ConstraintName = FString());

	void DestroyConstraint(FConstraintInstance* Constraint);

	// --- PhysicsAsset / Ragdoll Adapter ---
	// PhysicsAsset/Ragdoll 빌더가 사용할 body 생성/해제 진입점.
	// BodySetup 1개를 world transform 위치에 독립 body로 만든다 (compound BodyMappings와 별개).
	// 호출자가 반환 핸들을 보관하고, joint는 CreateConstraint로, 해제는 DestroyBody로 한다.
	FBodyInstance* CreateBodyFromBodySetup(UPrimitiveComponent* OwnerComp, UBodySetup* BodySetup,
		const FTransform& WorldTransform, bool bDynamic);
	void DestroyBody(FBodyInstance* Body);

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

#ifdef _DEBUG
	// PVD는 전역 PhysX 객체와 같이 공유
	// Scene 단위 소유가 아니기 때문에 관찰용 포인터만 보관
	physx::PxPvd* Pvd = nullptr;
	physx::PxPvdTransport* PvdTransport = nullptr;
#endif

	// Actor 단위 매핑 — 한 액터의 여러 컴포넌트가 같은 PxRigidActor에 shape로 합쳐진다.
	struct FBodyMapping
	{
		AActor* OwnerActor = nullptr;             // 키
		physx::PxRigidActor* Actor = nullptr;     // 기존 코드 호환용. 다음 단계에서 제거 또는 축소 예정.
		UPrimitiveComponent* RootComp = nullptr;  // 트랜스폼 동기화 기준
		TArray<UPrimitiveComponent*> Components;  // 등록된 컴포넌트들
	};
	// PxActor는 대표 UPrimitiveComponent::BodyInstance를 userData로 참조한다.
	// FBodyMapping은 Actor 단위 compound 관계만 추적한다.
	TArray<std::unique_ptr<FBodyMapping>> BodyMappings;

	// Constraint 는 PxRigidActor를 참조
	// Shutdown / body unregister시 Bodies보다 먼저 release
	TArray<std::unique_ptr<FConstraintInstance>> Constraints;
	TArray<USkeletalMeshComponent*> SkeletalPhysicsComponents;

	// Adapter(CreateBodyFromBodySetup)로 만든 독립 body. compound BodyMappings와 별개로
	// scene이 FBodyInstance를 소유하고, Shutdown / DestroyBody에서 PxRigidActor까지 정리한다.
	TArray<std::unique_ptr<FBodyInstance>> AdapterBodies;

	// 내부 헬퍼
	FBodyMapping* FindMappingByActor(AActor* OwnerActor);
	const FBodyMapping* FindMappingByActor(AActor* OwnerActor) const;
	FBodyMapping* FindMappingByComponent(UPrimitiveComponent* Comp);
	const FBodyMapping* FindMappingByComponent(UPrimitiveComponent* Comp) const;
	void DestroyConstraintsForBody(FBodyInstance* Body);
	void SyncPhysicsAssetBodiesToBones();

	// FPhysXShapeDesc 하나를 주어진 actor에 PxShape로 생성. 실패 시 nullptr.
	physx::PxShape* CreateShapeOnActor(physx::PxRigidActor* Actor, const FPhysXShapeDesc& Desc);

	// Comp의 geometry를 Mapping의 PxRigidActor에 shape로 추가. 실패 시 nullptr.
	physx::PxShape* AddShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp);
	// Comp의 BodySetup AggGeom을 Mapping의 PxRigidActor에 shape로 추가. shape가 하나 이상 생성되면 true.
	bool AddShapesFromBodySetup(FBodyMapping& Mapping, UPrimitiveComponent* Comp);
	// Mapping의 actor에서 Comp에 매칭된 shape를 detach.
	void DetachShapeForComponent(FBodyMapping& Mapping, UPrimitiveComponent* Comp);
};
