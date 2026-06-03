#include "Physics/BodyInstance.h"
#include "Physics/PhysX/PhysXHelper.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/AActor.h"

#include <PxPhysicsAPI.h>

using namespace physx;

void FBodyInstance::InitBody(UPrimitiveComponent* InOwnerComponent, physx::PxRigidActor* InRigidActor)
{
	OwnerComponent = InOwnerComponent;
	RigidActor = InRigidActor;

	if (OwnerComponent)
	{
		bSimulatePhysics = OwnerComponent->GetSimulatePhysics();
		bEnableGravity = true;
	}
}

void FBodyInstance::TerminateBody()
{
	// PxRigidActor Release는 FPhysXPhysicsScene이 소유
	OwnerComponent = nullptr;
	RigidActor = nullptr;

	CombinedComponents.clear();

	bSimulatePhysics = false;
	bEnableGravity = true;
	BoneIndex = -1;
	BodyIndex = -1;
}

bool FBodyInstance::IsValidBodyInstance() const
{
	return RigidActor != nullptr;
}

bool FBodyInstance::IsDynamic() const
{
	return GetPxRigidDynamic() != nullptr;
}

bool FBodyInstance::IsStatic() const
{
	return RigidActor && RigidActor->is<PxRigidStatic>() != nullptr;
}

bool FBodyInstance::IsKinematic() const
{
	PxRigidDynamic* Dynamic = GetPxRigidDynamic();
	if (!Dynamic)
	{
		return false;
	}

	return Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC;
}

bool FBodyInstance::IsSimulatingPhysics() const
{
	return bSimulatePhysics && IsDynamic() && !IsKinematic();
}

AActor* FBodyInstance::GetOwnerActor() const
{
	return OwnerComponent ? OwnerComponent->GetOwner() : nullptr;
}

UPrimitiveComponent* FBodyInstance::GetOwnerComponent() const
{
	return OwnerComponent;
}

void FBodyInstance::SetBoneIndex(int32 InBoneIndex)
{
	BoneIndex = InBoneIndex;
}

int32 FBodyInstance::GetBoneIndex() const
{
	return BoneIndex;
}

void FBodyInstance::SetBodyIndex(int32 InBodyIndex)
{
	BodyIndex = InBodyIndex;
}

int32 FBodyInstance::GetBodyIndex() const
{
	return BodyIndex;
}

physx::PxRigidActor* FBodyInstance::GetPxRigidActor() const
{
	return RigidActor;
}

physx::PxRigidDynamic* FBodyInstance::GetPxRigidDynamic() const
{
	return RigidActor ? RigidActor->is<PxRigidDynamic>() : nullptr;
}

FVector FBodyInstance::GetEngineWorldLocation()
{
	if (!RigidActor)
	{
		return FVector(0.f, 0.f, 0.f);
	}

	return FPhysXHelper::ToFVector(RigidActor->getGlobalPose().p);
}

FQuat FBodyInstance::GetEngineWorldRotation()
{
	if (!RigidActor)
	{
		return FQuat::Identity;
	}

	return FPhysXHelper::ToFQuat(RigidActor->getGlobalPose().q);
}

void FBodyInstance::SetBodyTransform(const FVector& WorldLocation, const FQuat& WorldRotation, bool bResetVelocity /*= false*/)
{
	if (!RigidActor) return;

	PxTransform NewPose = FPhysXHelper::ToPxTransform(WorldLocation, WorldRotation);

	if (PxRigidDynamic* Dynamic = GetPxRigidDynamic())
	{
		Dynamic->setGlobalPose(NewPose);
		// 정책: SetBodyTransform() 같은 함수가 
		// 물리 Body의 위치를 강제로 바꿀 때 속도까지 초기화할지 말지에 대한 규칙
		// kinematic body엔 velocity 개념이 없어 set*Velocity가 거부된다("Body must be non-kinematic!").
		// ragdoll 활성화는 이 teleport 직후 바디를 dynamic으로 바꾸고 그때 velocity는 0에서 시작하므로,
		// kinematic인 동안의 리셋은 스킵해도 결과가 같다.
		if (bResetVelocity && !(Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
		{
			Dynamic->setLinearVelocity(PxVec3(0.0f));
			Dynamic->setAngularVelocity(PxVec3(0.0f));
		}

		return;
	}

	RigidActor->setGlobalPose(NewPose);
}

void FBodyInstance::SetKinematicTarget(const FVector& WorldLocation, const FQuat& WorldRotation)
{
	PxRigidDynamic* Dynamic = GetPxRigidDynamic();
	if (!Dynamic)
	{
		return;
	}

	if (!(Dynamic->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
	{
		return;
	}

	Dynamic->setKinematicTarget(FPhysXHelper::ToPxTransform(WorldLocation, WorldRotation));
}

FVector FBodyInstance::GetLinearVelocity() const
{
	PxRigidDynamic* Dynamic = GetPxRigidDynamic();
	if (!Dynamic)
	{
		return FVector(0.f, 0.f, 0.f);
	}

	return FPhysXHelper::ToFVector(Dynamic->getLinearVelocity());
}

void FBodyInstance::SetLinearVelocity(const FVector& Velocity)
{
	PxRigidDynamic* Dynamic = GetPxRigidDynamic();
	if (!Dynamic)
	{
		return;
	}
	Dynamic->setLinearVelocity(FPhysXHelper::ToPxVec3(Velocity));
}

FVector FBodyInstance::GetAngularVelocity() const
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return FVector(0.f, 0.f, 0.f);
	}

	return FPhysXHelper::ToFVector(Dyn->getAngularVelocity());
}

void FBodyInstance::SetAngularVelocity(const FVector& Velocity)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->setAngularVelocity(FPhysXHelper::ToPxVec3(Velocity));
}

void FBodyInstance::AddForce(const FVector& Force)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->addForce(FPhysXHelper::ToPxVec3(Force));
}

void FBodyInstance::AddForceAtLocation(const FVector& Force, const FVector& WorldLocation)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	PxRigidBodyExt::addForceAtPos(
		*Dyn,
		FPhysXHelper::ToPxVec3(Force),
		FPhysXHelper::ToPxVec3(WorldLocation)
	);
}

void FBodyInstance::AddTorque(const FVector& Torque)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->addTorque(FPhysXHelper::ToPxVec3(Torque));
}

void FBodyInstance::AddImpulse(const FVector& Impulse)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->addForce(FPhysXHelper::ToPxVec3(Impulse), PxForceMode::eIMPULSE);
}

void FBodyInstance::AddImpulseAtLocation(const FVector& Impulse, const FVector& WorldLocation)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	PxRigidBodyExt::addForceAtPos(
		*Dyn,
		FPhysXHelper::ToPxVec3(Impulse),
		FPhysXHelper::ToPxVec3(WorldLocation),
		PxForceMode::eIMPULSE
	);
}

void FBodyInstance::AddAngularImpulse(const FVector& AngularImpulse)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->addTorque(FPhysXHelper::ToPxVec3(AngularImpulse), PxForceMode::eIMPULSE);
}

float FBodyInstance::GetBodyMass() const
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return 1.0f;
	}

	return Dyn->getMass();
}

void FBodyInstance::SetBodyMass(float NewMass)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	const float SafeMass = (NewMass > 0.0f) ? NewMass : 1.0f;

	PxVec3 LocalCOM = PxVec3(0.0f);
	if (OwnerComponent)
	{
		LocalCOM = FPhysXHelper::ToPxVec3(OwnerComponent->GetCenterOfMass());
	}

	// 질량 변경 시 inertia도 같이 갱신
	// COM을 명시 전달하지 않으면 이전 COM 설정이 shape 중심으로 덮일 수 있다.
	// COM: Center Of Mass
	PxRigidBodyExt::setMassAndUpdateInertia(*Dyn, SafeMass, &LocalCOM);
}

FVector FBodyInstance::GetCenterOfMassLocal() const
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return FVector(0, 0, 0);
	}

	return FPhysXHelper::ToFVector(Dyn->getCMassLocalPose().p);
}

void FBodyInstance::SetCenterOfMassLocal(const FVector& LocalOffset)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->setCMassLocalPose(PxTransform(FPhysXHelper::ToPxVec3(LocalOffset)));
}

void FBodyInstance::SetSimulatePhysics(bool bInSimulate)
{
	bSimulatePhysics = bInSimulate;

	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	// 현재 actor type 자체를 static <-> dynamic으로 바꾸지는 않음.
	// 그 작업은 FPhysXPhysicsScene::RebuildBody에서 처리
	if (bInSimulate)
	{
		Dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, false);
	}
	else
	{
		Dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
	}
}

void FBodyInstance::SetEnableGravity(bool bInEnableGravity)
{
	bEnableGravity = bInEnableGravity;

	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !bInEnableGravity);
}

void FBodyInstance::SetKinematic(bool bKinematic)
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, bKinematic);
}

void FBodyInstance::WakeInstance()
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->wakeUp();
}

void FBodyInstance::PutInstanceToSleep()
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return;
	}

	Dyn->putToSleep();
}

bool FBodyInstance::IsInstanceAwake() const
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return false;
	}

	return !Dyn->isSleeping();
}

bool FBodyInstance::IsInstanceSleeping() const
{
	PxRigidDynamic* Dyn = GetPxRigidDynamic();
	if (!Dyn)
	{
		return false;
	}

	return Dyn->isSleeping();
}
