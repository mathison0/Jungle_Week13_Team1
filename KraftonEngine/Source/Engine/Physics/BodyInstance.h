#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

class AActor;
class UPrimitiveComponent;

namespace physx
{
	class PxRigidActor;
	class PxRigidDynamic;
}

// =============================================================
// FBodyInstance
// - Runtime Physics Body Wrapper
// 
// 에셋 데이터가 아닌, 실제 Scene에 존재하는 물리 Body 1개를 나타낸다
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

	// --- 초기화 / 종료 ---
	void InitBody(UPrimitiveComponent* InOwnerComponent, physx::PxRigidActor* InRigidActor);
	void TerminateBody();

	// --- 기본 접근자 ---
	bool IsValidBodyInstance() const;
	bool IsDynamic() const;
	bool IsStatic() const;
	bool IsKinematic() const;
	bool IsSimulatingPhysics() const;

	AActor* GetOwnerActor() const;
	UPrimitiveComponent* GetOwnerComponent() const;

	physx::PxRigidActor* GetPxRigidActor() const;
	physx::PxRigidDynamic* GetPxRigidDynamic() const;

	void SetPxRigidActor(physx::PxRigidActor* InRigidActor);

	// --- Ragdoll / Physics Asset 준비용 식별자 ---
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
	UPrimitiveComponent* OwnerComponent = nullptr;
	physx::PxRigidActor* RigidActor = nullptr;

	// SkeletalMesh / Ragdoll
	// 일반 StaticMesh / ShapeComponent body는 -1.
	int32 BoneIndex = -1;
	int32 BodyIndex = -1;

	bool bSimulatePhysics = false;
	bool bEnableGravity = true;
};

