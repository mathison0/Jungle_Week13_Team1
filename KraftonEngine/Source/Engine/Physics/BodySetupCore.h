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
    // StaticMesh BodySetup에서는 None, SkeletalMesh PhysicsAsset BodySetup에서는 소유 본 이름.
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Bone Name")
    FName BoneName = FName::None;
};
