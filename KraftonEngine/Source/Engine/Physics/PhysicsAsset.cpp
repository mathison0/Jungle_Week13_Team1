#include "Physics/PhysicsAsset.h"

bool UPhysicsAsset::HasAnyBodySetup() const
{
    for (const UBodySetup* BodySetup : BodySetups)
    {
        if (BodySetup && BodySetup->HasGeometry())
        {
            return true;
        }
    }
    return false;
}

bool UPhysicsAsset::HasAnyConstraintSetup() const
{
    return !ConstraintSetups.empty();
}

int32 UPhysicsAsset::FindBodySetupIndexByBoneName(const FName& BoneName) const
{
    for (int32 Index = 0; Index < static_cast<int32>(BodySetups.size()); ++Index)
    {
        const UBodySetup* BodySetup = BodySetups[Index];
        if (BodySetup && BodySetup->GetBoneName() == BoneName)
        {
            return Index;
        }
    }
    return -1;
}

UBodySetup* UPhysicsAsset::FindBodySetupByBoneName(const FName& BoneName) const
{
    const int32 Index = FindBodySetupIndexByBoneName(BoneName);
    return Index >= 0 ? BodySetups[Index] : nullptr;
}
