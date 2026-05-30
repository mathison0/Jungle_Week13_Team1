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

    UPROPERTY(Edit, Save, Instanced, Category="Physics", DisplayName="Body Setups", Type=Array)
    TArray<UBodySetup*> BodySetups;

    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Constraint Setups", Type=Array, Struct=FConstraintInstance)
    TArray<FConstraintInstance> ConstraintSetups;
};
