#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Object/FName.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/Physics/ConstraintInstance.generated.h"

class FBodyInstance;

// 이동 자유도
enum class ELinearConstraintMotion : uint8
{
	Free = 0,		// 자유롭게 허용
	Limited,		// 일정 범위까지 허용
	Locked			// 완전히 막음
};

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
// ConstraintInstance가 실제 PxD6Joint같은 PhysX Joint를 만들 때 
// - 어떤 축을 막고
// - 어디까지 움직이게 하고
// - 얼마나 강하게 복구하고
// - 끊어질 수 있는지
// 
// 를 정하는 데이터
// ================================================================================
struct FConstraintOption
{
	// Linear DOF : 두 Body 사이에서 위치 이동을 얼마나 허용할 것인가?
	ELinearConstraintMotion XMotion = ELinearConstraintMotion::Locked;
	ELinearConstraintMotion YMotion = ELinearConstraintMotion::Locked;
	ELinearConstraintMotion ZMotion = ELinearConstraintMotion::Locked;

	// Limit 일 때 사용하는 거리제한 값
	float LinearLimit = 0.f;

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

	/*
	* projection: Joint가 크게 벌어질 때 Solver가 위치 보정하는 안정화 옵션
	* Solver: 물리 엔진에서 이번 프레임 물리 결과를 맞추기 위한 계산 단계
	*/
	bool bEnableProjection = true;						// 보정을 할 것인가?
	float ProjectionLinearTolerance = 1.0f;				// 두 Body의 Constraint가 이 거리보다 벌어지면 선형 Projection 수행
	float ProjectionAngularToleranceDegrees = 10.0f;	// Constraint 회전 오차가 이 각도보다 커지면 각도 Projection 수행

	// Drive: ragdoll 근육/physical animation에서 사용
	// Angular Drive: Constraint가 목표 회전으로 돌아가려고 힘을 내는 기능
	bool  bAngularDriveEnabled = false;		// 사용 여부
	float AngularDriveStiffness = 0.0f;		// 스프링 강도(높을수록 강하게 돌아가려고 함)
	float AngularDriveDamping = 0.0f;		// 감쇠 (높으면 천천히 안정적으로, 낮으면 목표를 지나쳐 흔들린다)
	float AngularDriveForceLimit = 0.0f;	// Drive가 내는 Force/Torque 크기 제한
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
