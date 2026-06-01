#include "PhysXPhysicsScene.h"

#include "Core/Logging/Log.h"
#include "Math/MathUtils.h"
#include "PhysXHelper.h"

#include <PxPhysicsAPI.h>

#include <algorithm>
#include <memory>

using namespace physx;

// ================================================================
// Constraint Instance
// ================================================================

// --- Constraint Helper Section ---
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

static bool IsAnySwingMotionLimited(const FConstraintOption& Option)
{
	return Option.Swing1Motion == EAngularConstraintMotion::Limited
		|| Option.Swing2Motion == EAngularConstraintMotion::Limited;
}

// Ragdoll v1 joint 설정:
// - Linear는 항상 Locked (본이 분리되지 않음)
// - Angular는 twist/swing limit만 적용
// - projection은 고정 기본값으로 켜서 joint가 크게 벌어졌을 때 위치를 보정한다 (안정화용)
static void ApplyContraintOptionToD6Joint(PxD6Joint* Joint, const FConstraintOption& Option)
{
	if (!Joint) return;

	// --- Linear DOF: ragdoll 본은 분리되지 않으므로 전부 Locked ---
	Joint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
	Joint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
	Joint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);

	// --- Angular DOF ---
	Joint->setMotion(PxD6Axis::eTWIST, ToPxD6Motion(Option.TwistMotion));
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

	// --- Projection (고정 기본값) ---
	// joint가 크게 벌어졌을 때 solver가 위치를 보정해 ragdoll 안정성을 높인다.
	// 세부 tolerance는 옵션으로 노출하지 않고 상수로 둔다.
	Joint->setConstraintFlag(PxConstraintFlag::ePROJECTION, true);
	Joint->setProjectionLinearTolerance(1.0f);
	Joint->setProjectionAngularTolerance(10.0f * FMath::DegToRad);
}

std::unique_ptr<FConstraintInstance> FPhysXPhysicsScene::CreateConstraint(
	FBodyInstance* Parent,
	FBodyInstance* Child,
	const FConstraintSetup& Setup)
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

	PxRigidActor* ParentActor = FPhysXHelper::GetRigidActor(Parent);
	PxRigidActor* ChildActor = FPhysXHelper::GetRigidActor(Child);

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
	NewConstraint->InitConstraint(Setup, Parent, Child);

	const PxTransform PxParentFrame = FPhysXHelper::ToPxTransform(Setup.ParentFrame);
	const PxTransform PxChildFrame = FPhysXHelper::ToPxTransform(Setup.ChildFrame);

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

	ApplyContraintOptionToD6Joint(Joint, Setup.Option);

	// Joint Relase는 FConstraintInstance::TerminateConstraint가 담당
	NewConstraint->SetConstraintHandle(Joint);

	// 소유권(unique_ptr)을 호출자(컴포넌트)에게 넘긴다.
	return NewConstraint;
}

void FPhysXPhysicsScene::DestroyConstraint(FConstraintInstance* Constraint)
{
	// PxJoint만 해제한다. FConstraintInstance 객체는 소유자(컴포넌트 Constraints)가 삭제한다.
	if (!Constraint) return;
	Constraint->TerminateConstraint();
}
