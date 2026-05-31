#pragma once

#include "Object/Object.h"
#include "Physics/BodySetup.h"
#include "Physics/ConstraintInstance.h"

#include "Source/Engine/Physics/PhysicsAsset.generated.h"

UCLASS()
class UPhysicsAsset : public UObject
{
public:
    GENERATED_BODY()

    UPhysicsAsset() = default;
    ~UPhysicsAsset() override = default;

    // PhysicsAsset Editor에서 bone별 collision body를 편집하는 목록.
    // 각 BodySetup의 BoneName은 skeleton에 실제로 존재해야 하며, bone 하나당 하나를 권장한다.
    UPROPERTY(Edit, Save, Instanced, Category="Physics", DisplayName="Body Setups", Type=Array)
    TArray<UBodySetup*> BodySetups;

    // 두 body를 연결하는 ragdoll joint 설정 목록.
    // ParentBoneName과 ChildBoneName은 위 BodySetups에 등록된 bone 이름을 가리켜야 한다.
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Constraint Setups", Type=Array, Struct=FConstraintInstance)
    TArray<FConstraintInstance> ConstraintSetups;

    bool HasAnyBodySetup() const;
    bool HasAnyConstraintSetup() const;
    int32 FindBodySetupIndexByBoneName(const FName& BoneName) const;
    UBodySetup* FindBodySetupByBoneName(const FName& BoneName) const;
};
