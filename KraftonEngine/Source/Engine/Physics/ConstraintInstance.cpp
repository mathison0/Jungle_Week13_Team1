#include "Physics/ConstraintInstance.h"

void FConstraintInstance::InitConstraint(FBodyInstance* InParentBody, FBodyInstance* InChildBody)
{
    ParentBody = InParentBody;
    ChildBody = InChildBody;
}

void FConstraintInstance::TerminateConstraint()
{
    // TODO(A): Joint lifecycle owner가 FConstraintInstance로 확정되면
    // 여기서 Joint->release()를 호출하고 nullptr로 정리한다.
    Joint = nullptr;
    ParentBody = nullptr;
    ChildBody = nullptr;
}

bool FConstraintInstance::IsValidConstraint() const
{
    return ParentBody != nullptr && ChildBody != nullptr;
}
