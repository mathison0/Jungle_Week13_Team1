#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Object/FName.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/Physics/ConstraintInstance.generated.h"

class FBodyInstance;

namespace physx
{
    class PxJoint;
}

USTRUCT()
struct FConstraintInstance
{
    GENERATED_BODY()

    FString ConstraintName;

    FName ParentBoneName;
    FName ChildBoneName;

    FTransform ParentFrame;
    FTransform ChildFrame;

    bool bTwistLimited = true;
    float TwistLimitMinDegrees = -45.0f;
    float TwistLimitMaxDegrees = 45.0f;

    bool bSwingLimited = true;
    float Swing1LimitDegrees = 30.0f;
    float Swing2LimitDegrees = 30.0f;

    // TODO(B)
    // UPhysicsAsset은 asset template만 들고, USkeletalMeshComponent가 runtime
    // Bodies/Constraints 배열을 소유한다. PhysicsAsset instantiate 단계에서
    // ParentBoneName/ChildBoneName을 USkeletalMeshComponent::Bodies의
    // FBodyInstance로 resolve한다.
    FBodyInstance* ParentBody = nullptr;
    FBodyInstance* ChildBody = nullptr;

    // TODO(A): PhysX joint 생성 책임 위치가 확정되면 PxD6JointCreate 결과를 여기에 저장한다.
    // A 쪽 결정 필요: FConstraintInstance가 직접 create/release할지,
    // FPhysXPhysicsScene 또는 별도 PhysicsAssetInstance가 lifecycle을 소유할지.
    physx::PxJoint* Joint = nullptr;

    // TODO(A): InitConstraint 호출 시점과 입력 계약 확정 필요.
    // 예상 경로: UPhysicsAsset의 template constraint를 runtime constraint로 복사한 뒤,
    // resolved ParentBody/ChildBody를 넘겨 초기화한다.
    void InitConstraint(FBodyInstance* InParentBody, FBodyInstance* InChildBody);

    // TODO(A): Joint가 실제 생성된 뒤에는 release 주체에 맞춰 TerminateConstraint에서
    // PxJoint::release를 호출할지, 단순 포인터 정리만 할지 결정해야 한다.
    void TerminateConstraint();
    bool IsValidConstraint() const;
};
