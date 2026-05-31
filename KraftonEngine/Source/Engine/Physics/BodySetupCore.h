#pragma once

#include "Object/Object.h"

#include "Source/Engine/Physics/BodySetupCore.generated.h"

UCLASS()
class UBodySetupCore : public UObject
{
public:
    GENERATED_BODY()

    UBodySetupCore() = default;
    ~UBodySetupCore() override = default;

    const FName& GetBoneName() const { return BoneName; }
    void SetBoneName(const FName& InBoneName) { BoneName = InBoneName; }

protected:
    // StaticMesh BodySetup에서는 None으로 둔다.
    // PhysicsAsset Editor에서 생성하는 SkeletalMesh BodySetup은 소유 bone 이름을 넣어야 한다.
    // 런타임 ragdoll 생성 시 이 이름으로 skeleton bone과 body를 연결한다.
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Bone Name")
    FName BoneName = FName::None;
};
