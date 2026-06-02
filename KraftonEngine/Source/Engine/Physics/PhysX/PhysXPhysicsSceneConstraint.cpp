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
// - Angular twist/swing은 soft limit(spring)으로 적용
// - projection은 끈다 (soft limit의 부드러운 보정을 기하학적 snap이 우회하지 않도록)
static void ApplyContraintOptionToD6Joint(PxD6Joint* Joint, const FConstraintOption& Option)
{
	if (!Joint) return;

	// --- Soft limit spring ---
	// limit을 hard(딱 막기)가 아니라 spring으로 둔다. 위반 시 보정이 "속도(= 위반/dt, dt 작으면 폭발)"가
	// 아니라 "힘(= Stiffness×위반 − Damping×속도, 유한)"이라 프레임률(dt)과 무관하고 폭발하지 않는다.
	// 게다가 시작 포즈가 limit을 넘었어도(capoeira 같은 극단 死포즈) bind 기준 정상 범위로 부드럽게 복귀한다.
	// → limit 기준은 bind 그대로라 해부학적으로 맞고, 발사도 없다.
	// ease-back 최대 속도 ≈ (Stiffness/Damping)×위반각 이라 그 비율로 튜닝한다(낮을수록 부드러움).
	constexpr float LimitSpringStiffness = 200.0f;
	constexpr float LimitSpringDamping = 60.0f;
	const PxSpring LimitSpring(LimitSpringStiffness, LimitSpringDamping);

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
		Joint->setTwistLimit(PxJointAngularLimitPair(-TwistLimitRad, TwistLimitRad, LimitSpring));
	}

	if (IsAnySwingMotionLimited(Option))
	{
		const float Swing1Rad = FMath::ClampMin(Option.Swing1LimitDegrees * FMath::DegToRad, 0.1f * FMath::DegToRad);
		const float Swing2Rad = FMath::ClampMin(Option.Swing2LimitDegrees * FMath::DegToRad, 0.1f * FMath::DegToRad);

		// Swing1 / Swing2는 Cone Limit으로 묶어서 적용 (soft)
		Joint->setSwingLimit(PxJointLimitCone(Swing1Rad, Swing2Rad, LimitSpring));
	}

	// --- Projection OFF ---
	// projection은 위반된 joint를 매 프레임 "기하학적으로" 되붙인다(snap). 이게 soft limit의 부드러운
	// spring 보정을 우회해 첫 프레임에 다시 캐릭터를 튕길 수 있으므로 끈다. linear는 Locked + 속도 솔버가
	// 잡고, 본 없는 중간 bone로 인한 위치 어긋남은 SyncPhysicsAssetBodiesToComponentPose가 보정한다.
	Joint->setConstraintFlag(PxConstraintFlag::ePROJECTION, false);
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
