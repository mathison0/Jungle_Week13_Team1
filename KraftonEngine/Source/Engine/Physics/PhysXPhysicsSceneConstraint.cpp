#include "Physics/PhysXPhysicsScene.h"

#include "Core/Logging/Log.h"
#include "Math/MathUtils.h"
#include "Physics/PhysXHelper.h"

#include <PxPhysicsAPI.h>

#include <algorithm>
#include <memory>

using namespace physx;

// ================================================================
// Constraint Instance
// ================================================================

// --- Constraint Helper Section ---
static PxD6Motion::Enum ToPxD6Motion(ELinearConstraintMotion Motion)
{
	switch (Motion)
	{
	case ELinearConstraintMotion::Free:		return PxD6Motion::eFREE;
	case ELinearConstraintMotion::Limited:	return PxD6Motion::eLIMITED;
	case ELinearConstraintMotion::Locked:
	default:								return PxD6Motion::eLOCKED;
	}
}

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

static bool IsAnyLinearMotionLimited(const FConstraintOption& Option)
{
	return Option.XMotion == ELinearConstraintMotion::Limited
		|| Option.YMotion == ELinearConstraintMotion::Limited
		|| Option.ZMotion == ELinearConstraintMotion::Limited;
}

static bool IsAnySwingMotionLimited(const FConstraintOption& Option)
{
	return Option.Swing1Motion == EAngularConstraintMotion::Limited
		|| Option.Swing2Motion == EAngularConstraintMotion::Limited;
}

static bool IsAnyAngularMotionFreeOrLimited(const FConstraintOption& Option)
{
	return Option.TwistMotion != EAngularConstraintMotion::Locked
		|| Option.Swing1Motion != EAngularConstraintMotion::Locked
		|| Option.Swing2Motion != EAngularConstraintMotion::Locked;
}

static void ApplyContraintOptionToD6Joint(PxD6Joint* Joint,
	const FConstraintOption& Option, const PxTolerancesScale& Scale)
{
	if (!Joint) return;

	// --- Linear DOF ---
	Joint->setMotion(PxD6Axis::eX, ToPxD6Motion(Option.XMotion));
	Joint->setMotion(PxD6Axis::eY, ToPxD6Motion(Option.YMotion));
	Joint->setMotion(PxD6Axis::eZ, ToPxD6Motion(Option.ZMotion));

	if (IsAnyLinearMotionLimited(Option))
	{
		const float LinearLimit = FMath::ClampMin(Option.LinearLimit, 0.0f);

		// Physx D6의 Linear Limit은 Limited Linear Axis 전체에 공통 적용
		Joint->setLinearLimit(PxJointLinearLimit(Scale, LinearLimit));
	}

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

	// --- Projection ---
	Joint->setConstraintFlag(PxConstraintFlag::ePROJECTION, Option.bEnableProjection);
	if (Option.bEnableProjection)
	{
		Joint->setProjectionLinearTolerance(FMath::ClampMin(Option.ProjectionLinearTolerance, 0.0f));
		Joint->setProjectionAngularTolerance(FMath::ClampMin(Option.ProjectionAngularToleranceDegrees * FMath::DegToRad, 0.0f));
	}

	// --- Angular Drive ---
	if (Option.bAngularDriveEnabled && IsAnyAngularMotionFreeOrLimited(Option))
	{
		const float ForceLimit = Option.AngularDriveForceLimit > 0.0f ? Option.AngularDriveForceLimit : PX_MAX_F32;
		const PxD6JointDrive Drive(
			FMath::ClampMin(Option.AngularDriveStiffness, 0.0f),
			FMath::ClampMin(Option.AngularDriveDamping, 0.0f),
			ForceLimit,
			false);

		// Slerp Drive
		Joint->setDrive(PxD6Drive::eSLERP, Drive);
		Joint->setDrivePosition(PxTransform(PxIdentity));
	}
}

FConstraintInstance* FPhysXPhysicsScene::CreateConstraint(FBodyInstance* Parent, FBodyInstance* Child,
	const FConstraintOption& Option, const FTransform& ParentFrame, const FTransform& ChildFrame, const FString& ConstraintName /*= FString()*/)
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

	PxRigidActor* ParentActor = Parent->GetPxRigidActor();
	PxRigidActor* ChildActor = Child->GetPxRigidActor();

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

	NewConstraint->ConstraintName = ConstraintName;
	NewConstraint->ParentFrame = ParentFrame;
	NewConstraint->ChildFrame = ChildFrame;
	NewConstraint->Option = Option;
	NewConstraint->InitConstraint(Parent, Child);

	const PxTransform PxParentFrame = FPhysXHelper::ToPxTransform(ParentFrame);
	const PxTransform PxChildFrame = FPhysXHelper::ToPxTransform(ChildFrame);

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

	ApplyContraintOptionToD6Joint(Joint, Option, Physics->getTolerancesScale());

	// Joint Relase는 FConstraintInstance::TerminateConstraint가 담당
	NewConstraint->SetConstraintHandle(Joint);

	FConstraintInstance* Result = NewConstraint.get();
	Constraints.push_back(std::move(NewConstraint));

	return Result;
}

void FPhysXPhysicsScene::DestroyConstraint(FConstraintInstance* Constraint)
{
	if (!Constraint) return;

	Constraint->TerminateConstraint();

	Constraints.erase(
		std::remove_if(
			Constraints.begin(),
			Constraints.end(),
			[Constraint](const std::unique_ptr<FConstraintInstance>& Ptr)
			{
				return Ptr.get() == Constraint;
			}
		),
		Constraints.end()
	);
}

void FPhysXPhysicsScene::DestroyConstraintsForBody(FBodyInstance* BodyInstance)
{
	/*
	 * Body가 제거될 때 해당 body를 참조하는 joint가 남으면,
	 * PhysX actor release 이후 joint가 죽은 actor를 물고 있게 됩니다.
	 * 그래서 body release 전에 연결된 constraint를 먼저 지워야 합니다.
	 */
	if (!BodyInstance) return;

	Constraints.erase(std::remove_if(Constraints.begin(), Constraints.end(),
		[BodyInstance](const std::unique_ptr<FConstraintInstance>& Ptr)
		{
			if (!Ptr) return true;
			if (Ptr->ParentBody == BodyInstance || Ptr->ChildBody == BodyInstance)
			{
				Ptr->TerminateConstraint();
				return true;
			}
			return false;
		}),
		Constraints.end()
	);
}
