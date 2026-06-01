#include "Physics/ConstraintInstance.h"
#include "Physics/BodyInstance.h"

#include <PxPhysicsAPI.h>

using namespace physx;

void FConstraintInstance::InitConstraint(const FConstraintSetup& InSetup, FBodyInstance* InParentBody, FBodyInstance* InChildBody)
{
    Setup = InSetup;
    ParentBody = InParentBody;
    ChildBody = InChildBody;
}

void FConstraintInstance::SetConstraintHandle(physx::PxJoint* InHandle)
{
	if (JointHandle == InHandle)
	{
		return;
	}
	ReleaseJointHandle();

	JointHandle = InHandle;
}

void FConstraintInstance::TerminateConstraint()
{
	ReleaseJointHandle();

    ParentBody = nullptr;
    ChildBody = nullptr;
}

void FConstraintInstance::ReleaseJointHandle()
{
	// FConstraintInstance는 PxJoint 핸들을 소유/래핑한다.
	// 따라서 자신이 가진 PxJoint의 release까지 담당한다.
	if (JointHandle)
	{
		JointHandle->release();
		JointHandle = nullptr;
	}
}

bool FConstraintInstance::IsValidConstraint() const
{
    return ParentBody != nullptr && ChildBody != nullptr && JointHandle != nullptr;
}
