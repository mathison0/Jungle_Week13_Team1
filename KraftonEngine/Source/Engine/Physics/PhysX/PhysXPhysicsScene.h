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

	// 살아있는 강체들의 대표 body 목록. 객체 소유는 컴포넌트가 하고 여기엔 포인터만 둔다(매 프레임 순회용).
	// 한 강체에 여러 컴포넌트가 합쳐져도 대표 하나만 여기 들어간다.
	TArray<FBodyInstance*> Bodies;

	// Constraint 는 PxRigidActor를 참조
	// Shutdown / body unregister시 Bodies보다 먼저 release
	TArray<std::unique_ptr<FConstraintInstance>> Constraints;
	TArray<USkeletalMeshComponent*> SkeletalPhysicsComponents;

	// Adapter(CreateBodyFromBodySetup)로 만든 독립 body. compound BodyMappings와 별개로
	// scene이 FBodyInstance를 소유하고, Shutdown / DestroyBody에서 PxRigidActor까지 정리한다.
	TArray<std::unique_ptr<FBodyInstance>> AdapterBodies;

	// 내부 헬퍼
	// Comp가 속한 강체의 대표 body. 등록 안 됐으면 nullptr.
	FBodyInstance* FindHostBody(UPrimitiveComponent* Comp);
	const FBodyInstance* FindHostBody(UPrimitiveComponent* Comp) const;
	// Actor의 강체 대표 body. ragdoll/adapter body는 제외.
	FBodyInstance* FindHostBodyByActor(AActor* OwnerActor);
	void DestroyConstraintsForBody(FBodyInstance* Body);
	// 강체 하나의 PhysX 자원을 해제하는 공통 경로(joint → actor 순). FBodyInstance 객체는 소유자가 지우므로 여기서 delete하지 않는다.
	void ReleaseBodyResource(FBodyInstance* Body);
	void SyncPhysicsAssetBodiesToBones();

	// FPhysXShapeDesc 하나를 주어진 actor에 PxShape로 생성. 실패 시 nullptr.
	physx::PxShape* CreateShapeOnActor(physx::PxRigidActor* Actor, const FPhysXShapeDesc& Desc);

	// HostActor에 Comp의 geometry를 shape로 추가(RootComp 기준 LocalPose). 실패 시 nullptr.
	physx::PxShape* AddShapeForComponent(physx::PxRigidActor* HostActor, UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp);
	// HostActor에 Comp의 BodySetup AggGeom을 shape로 추가. shape가 하나 이상 생성되면 true.
	bool AddShapesFromBodySetup(physx::PxRigidActor* HostActor, UPrimitiveComponent* RootComp, UPrimitiveComponent* Comp);
	// HostActor에서 Comp에 매칭된 shape를 detach.
	void DetachShapeForComponent(physx::PxRigidActor* HostActor, UPrimitiveComponent* Comp);
};
