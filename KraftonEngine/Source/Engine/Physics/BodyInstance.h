#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

class AActor;
class FPhysXHelper;
class FPhysXPhysicsScene;
class UPrimitiveComponent;

namespace physx
{
	class PxRigidActor;
	class PxRigidDynamic;
}

// =============================================================
// FBodyInstance
//
// 에셋 데이터가 아니라 실제 Scene에 생성된 물리 Body를 가리키는 런타임 wrapper.
// owner component와 backend body handle을 들고, body 상태 조회와
// force / velocity / mass / center-of-mass 조작만 담당한다.
// =============================================================

class FBodyInstance
{
public:
	FBodyInstance() = default;
	~FBodyInstance() = default;

	FBodyInstance(const FBodyInstance&) = delete;
	FBodyInstance& operator=(const FBodyInstance&) = delete;

	FBodyInstance(FBodyInstance&&) = delete;
	FBodyInstance& operator=(FBodyInstance&&) = delete;

	// --- 종료 ---
	void TerminateBody();

	// --- 기본 접근자 ---
	bool IsValidBodyInstance() const;
	bool IsDynamic() const;
	bool IsStatic() const;
	bool IsKinematic() const;
	bool IsSimulatingPhysics() const;

	AActor* GetOwnerActor() const;
	UPrimitiveComponent* GetOwnerComponent() const;

	// --- Ragdoll / Physics Asset identifiers ---
	void SetBoneIndex(int32 InBoneIndex);
	int32 GetBoneIndex() const;
	void SetBodyIndex(int32 InBodyIndex);
	int32 GetBodyIndex() const;

	// --- Transform --- 
	FVector GetEngineWorldLocation();
	FQuat	GetEngineWorldRotation();

	void SetBodyTransform(const FVector& WorldLocation, const FQuat& WorldRotation, bool bResetVelocity = false);

	// --- Kinematic body 전용 ---
	void SetKinematicTarget(const FVector& WorldLocation, const FQuat& WorldRotation);

	// --- Velocity ---
	FVector GetLinearVelocity() const;
	void SetLinearVelocity(const FVector& Velocity);

	FVector GetAngularVelocity() const;
	void SetAngularVelocity(const FVector& Velocity);

	// --- Force / Torque ---
	void AddForce(const FVector& Force);
	void AddForceAtLocation(const FVector& Force, const FVector& WorldLocation);
	void AddTorque(const FVector& Torque);
	void AddImpulse(const FVector& Impulse);
	void AddImpulseAtLocation(const FVector& Impulse, const FVector& WorldLocation);
	void AddAngularImpulse(const FVector& AngularImpulse);

	// --- Mass / Center of Mass ---
	float GetBodyMass() const;
	void SetBodyMass(float NewMass);

	FVector GetCenterOfMassLocal() const;
	void SetCenterOfMassLocal(const FVector& LocalOffset);

	// --- flags ----
	void SetSimulatePhysics(bool bInSimulate);
	void SetEnableGravity(bool bInEnableGravity);
	void SetKinematic(bool bKinematic);

	// -- Sleep / Wake ---
	void WakeInstance();
	void PutInstanceToSleep();
	bool IsInstanceAwake() const;
	bool IsInstanceSleeping() const;

private:
	friend class FPhysXHelper;
	friend class FPhysXPhysicsScene;

	// --- 초기화 ---
	void InitBody(UPrimitiveComponent* InOwnerComponent, physx::PxRigidActor* InRigidActor);

	physx::PxRigidActor* GetPxRigidActor() const;
	physx::PxRigidDynamic* GetPxRigidDynamic() const;

	UPrimitiveComponent* OwnerComponent = nullptr;
	physx::PxRigidActor* RigidActor = nullptr;

	// 한 액터의 여러 컴포넌트가 한 강체로 합쳐질 때, 이 body가 대표면 같은 강체에 충돌 모양을 얹은
	// 컴포넌트들의 목록. 대표 컴포넌트는 OwnerComponent. (ragdoll 같은 단독 body는 비어 있음)
	TArray<UPrimitiveComponent*> CombinedComponents;

	int32 BoneIndex = -1;
	int32 BodyIndex = -1;

	bool bSimulatePhysics = false;
	bool bEnableGravity = true;
};

