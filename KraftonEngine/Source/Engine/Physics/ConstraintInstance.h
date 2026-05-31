#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Object/FName.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/Physics/ConstraintInstance.generated.h"

class FBodyInstance;

// 회전 자유도
enum class EAngularConstraintMotion : uint8
{
	Free = 0,
	Limited,
	Locked
};

namespace physx
{
	class PxJoint;
}

// ================================================================================
// FConstraintOption
// - Ragdoll v1 joint이 두 Body 사이 회전을 어디까지 허용할지 정하는 데이터.
// - Linear는 항상 Locked(본이 분리되지 않음)라 옵션으로 두지 않고 joint 생성 시 하드코딩한다.
// - angular drive / projection 세부 옵션 / physical animation 설정은 이번 구현에서 제외한다.
// ================================================================================
struct FConstraintOption
{
	// Angular DOF: 두 Body 사이 회전을 얼마나 허용할지 정하는 값
	// ragdoll이 너무 비현실적으로 꺾이지 않게 조절하는 값
	/*
	*						개념								예시
	*	Twist  = 관절의 주축을 기준으로 비트는 회전	(팔을 축으로 돌리는 회전) -> 비트는 회전 제어
	*	Swing1 = 주축에서 한 방향으로 꺾이는 회전		(팔을 위아래로 드는 회전) -> 관절이 꺾이는 회전 제어
	*	Swing2 = 주축에서 다른 방향으로 꺾이는 회전	(팔을 좌우로 벌리는 회전) -> 관절이 꺾이는 회전 제어
	*/
	EAngularConstraintMotion TwistMotion = EAngularConstraintMotion::Limited;
	EAngularConstraintMotion Swing1Motion = EAngularConstraintMotion::Limited;
	EAngularConstraintMotion Swing2Motion = EAngularConstraintMotion::Limited;

	// 각도 제한
	float TwistLimitDegrees = 45.f;
	float Swing1LimitDegrees = 30.f;
	float Swing2LimitDegrees = 30.f;
};

// ============================================================================
// ConstraintInstance
// - 두 개의 BodyInstance 사이를 어떻게 묶을지 저장하고, 실제 PhysX Joint/Constraint를 생성·관리하는 객체
// 
// 역할 분리
// - FPhysXPhysicsScene
//   - ConstraintInstance 목록을 등록/관리한다
//   - Scene Shutdown 시 Constraint -> Body 순서로 종료를 호출한다
//   - PxJoint를 직접 release하지 않고 FConstraintInstance::TerminateConstraint()를 호출한다
//
// - FConstraintInstance
//   - 개별 PxJoint 핸들을 소유/래핑한다
//   - InitConstraint()에서 PxJoint를 생성하고 설정한다
//   - TerminateConstraint()에서 자신이 가진 PxJoint를 release한다
//
// - 종료 순서
//   - Joint는 PxRigidActor를 참조하므로 Constraints를 먼저 release한다
//   - 그 다음 Bodies를 release한다
// ============================================================================
USTRUCT()
struct FConstraintInstance
{
    GENERATED_BODY()

    FString ConstraintName;

    FName ParentBoneName;
    FName ChildBoneName;

	// Body Local 기준 Joint Frame
    FTransform ParentFrame;	// ParentBody 로컬 공간에 있는 Joint 기준 좌표계
    FTransform ChildFrame;	// ChildBody 로컬 공간에 있는 Joint 기준 좌표계

	// Constraint Option
	FConstraintOption Option;

    // 런타임에 이름/컴포넌트 참조를 통해 찾아낸 실제 BodyInstance
    FBodyInstance* ParentBody = nullptr;
    FBodyInstance* ChildBody = nullptr;

    void InitConstraint(FBodyInstance* InParentBody, FBodyInstance* InChildBody);
	void TerminateConstraint();

	void SetConstraintHandle(physx::PxJoint* InHandle);
	physx::PxJoint* GetJointHandle() const { return JointHandle; }

	bool IsValidConstraint() const;

private:
	physx::PxJoint* JointHandle = nullptr;
	
	void ReleaseJointHandle();
    
};
