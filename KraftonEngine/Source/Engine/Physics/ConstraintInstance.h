#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"

class FBodyInstance;

namespace physx
{
    class PxJoint;
}

struct FConstraintInstance
{
    FString ConstraintName;

    FString ParentBoneName;
    FString ChildBoneName;

    FTransform ParentFrame;
    FTransform ChildFrame;

    FBodyInstance* ParentBody = nullptr;
    FBodyInstance* ChildBody = nullptr;

    physx::PxJoint* Joint = nullptr;
};