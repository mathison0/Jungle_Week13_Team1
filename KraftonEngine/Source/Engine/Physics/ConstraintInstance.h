#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"
#include "Object/FName.h"
#include "Object/Reflection/ObjectMacros.h"
#include "Object/Reflection/UStruct.h"

#include "Source/Engine/Physics/ConstraintInstance.generated.h"

class FBodyInstance;

// 회전 자유도
UENUM()
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
USTRUCT()
struct FConstraintOption
{
	GENERATED_BODY()

	// Angular DOF: 두 Body 사이 회전을 얼마나 허용할지 정하는 값
	// ragdoll이 너무 비현실적으로 꺾이지 않게 조절하는 값
	/*
	*						개념								예시
	*	Twist  = 관절의 주축을 기준으로 비트는 회전	(팔을 축으로 돌리는 회전) -> 비트는 회전 제어
	*	Swing1 = 주축에서 한 방향으로 꺾이는 회전		(팔을 위아래로 드는 회전) -> 관절이 꺾이는 회전 제어
	*	Swing2 = 주축에서 다른 방향으로 꺾이는 회전	(팔을 좌우로 벌리는 회전) -> 관절이 꺾이는 회전 제어
	*/
	UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Twist Motion", Enum=EAngularConstraintMotion)
	EAngularConstraintMotion TwistMotion = EAngularConstraintMotion::Limited;
	UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Swing 1 Motion", Enum=EAngularConstraintMotion)
	EAngularConstraintMotion Swing1Motion = EAngularConstraintMotion::Limited;
	UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Swing 2 Motion", Enum=EAngularConstraintMotion)
	EAngularConstraintMotion Swing2Motion = EAngularConstraintMotion::Limited;

	// 각도 제한
	UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Twist Limit Degrees", Min=0.0f, Speed=1.0f)
	float TwistLimitDegrees = 45.f;
	UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Swing 1 Limit Degrees", Min=0.0f, Speed=1.0f)
	float Swing1LimitDegrees = 30.f;
	UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Swing 2 Limit Degrees", Min=0.0f, Speed=1.0f)
	float Swing2LimitDegrees = 30.f;
};

// ============================================================================
// ConstraintSetup
// - PhysicsAsset에 저장되는 관절 템플릿 데이터.
// - 실제 runtime body 포인터나 PhysX joint handle은 들지 않는다.
// ============================================================================
USTRUCT()
struct FConstraintSetup
{
    GENERATED_BODY()

    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Constraint Name")
    FName ConstraintName;

    // PhysicsAsset Editor에서 선택하는 연결 대상 bone.
    // 두 이름 모두 UPhysicsAsset::BodySetups에 등록되어 있어야 runtime PxD6Joint가 생성된다.
    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Parent Bone")
    FName ParentBoneName;
    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Child Bone")
    FName ChildBoneName;

    // Body local 기준 joint frame.
    // Editor gizmo에서 joint 위치와 축을 편집할 때 각 body 로컬 공간으로 변환해 저장한다.
    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Parent Frame Location", Member=ParentFrame.Location, Type=Vec3, Speed=0.1f);
    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Parent Frame Rotation", Member=ParentFrame.Rotation, Type=Vec4, Speed=0.01f);
    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Parent Frame Scale", Member=ParentFrame.Scale, Type=Vec3, Speed=0.1f);
    FTransform ParentFrame;

    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Child Frame Location", Member=ChildFrame.Location, Type=Vec3, Speed=0.1f);
    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Child Frame Rotation", Member=ChildFrame.Rotation, Type=Vec4, Speed=0.01f);
    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Child Frame Scale", Member=ChildFrame.Scale, Type=Vec3, Speed=0.1f);
    FTransform ChildFrame;

    UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Option", Type=Struct, Struct=FConstraintOption)
	FConstraintOption Option;
};

// ============================================================================
// ConstraintInstance
// - 두 개의 BodyInstance 사이에 생성된 실제 PhysX Joint/Constraint를 관리하는 객체
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

	UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Constraint Name")
    FString ConstraintName;

    // PhysicsAsset Editor에서 선택하는 연결 대상 bone.
    // 두 이름 모두 UPhysicsAsset::BodySetups에 등록되어 있어야 runtime PxD6Joint가 생성된다.
	UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Parent Bone")
    FName ParentBoneName;
	UPROPERTY(Edit, Save, Category="Physics|Constraint", DisplayName="Child Bone")
    FName ChildBoneName;

    // Body local 기준 joint frame.
    // Editor gizmo에서 joint 위치와 축을 편집할 때 각 body 로컬 공간으로 변환해 저장한다.
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Parent Frame", DisplayName="Parent Location", Member=ParentFrame.Location, Type=Vec3, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Parent Frame", DisplayName="Parent Rotation", Member=ParentFrame.Rotation, Type=Vec4, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Parent Frame", DisplayName="Parent Scale", Member=ParentFrame.Scale, Type=Vec3, Speed=0.1f);
    FTransform ParentFrame;	// ParentBody 로컬 공간에 있는 Joint 기준 좌표계
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Child Frame", DisplayName="Child Location", Member=ChildFrame.Location, Type=Vec3, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Child Frame", DisplayName="Child Rotation", Member=ChildFrame.Rotation, Type=Vec4, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Child Frame", DisplayName="Child Scale", Member=ChildFrame.Scale, Type=Vec3, Speed=0.1f);
    FTransform ChildFrame;	// ChildBody 로컬 공간에 있는 Joint 기준 좌표계

	// Constraint Option
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Limits", DisplayName="Twist Motion", Member=Option.TwistMotion, Enum=EAngularConstraintMotion);
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Limits", DisplayName="Swing 1 Motion", Member=Option.Swing1Motion, Enum=EAngularConstraintMotion);
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Limits", DisplayName="Swing 2 Motion", Member=Option.Swing2Motion, Enum=EAngularConstraintMotion);
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Limits", DisplayName="Twist Limit", Member=Option.TwistLimitDegrees, Type=Float, Min=0.1f, Speed=0.5f);
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Limits", DisplayName="Swing 1 Limit", Member=Option.Swing1LimitDegrees, Type=Float, Min=0.1f, Speed=0.5f);
	UPROPERTY(Edit, Save, Category="Physics|Constraint|Limits", DisplayName="Swing 2 Limit", Member=Option.Swing2LimitDegrees, Type=Float, Min=0.1f, Speed=0.5f);
	FConstraintOption Option;
    FConstraintSetup Setup;

    // 런타임에 이름/컴포넌트 참조를 통해 찾아낸 실제 BodyInstance
    FBodyInstance* ParentBody = nullptr;
    FBodyInstance* ChildBody = nullptr;

    void InitConstraint(const FConstraintSetup& InSetup, FBodyInstance* InParentBody, FBodyInstance* InChildBody);
	void TerminateConstraint();

	void SetConstraintHandle(physx::PxJoint* InHandle);
	physx::PxJoint* GetJointHandle() const { return JointHandle; }

	bool IsValidConstraint() const;

private:
	physx::PxJoint* JointHandle = nullptr;
	
	void ReleaseJointHandle();
    
};
