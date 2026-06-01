#pragma once

#include "Asset/AssetRegistry.h"
#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"

class FReferenceCollector;
class UPhysicsAsset;

class FPhysicsAssetManager : public TSingleton<FPhysicsAssetManager>
{
    friend class TSingleton<FPhysicsAssetManager>;

public:
    UPhysicsAsset* Load(const FString& Path);
    UPhysicsAsset* Find(const FString& Path) const;
    bool Save(UPhysicsAsset* Asset, const FString& SourcePath = FString());

    void RefreshAvailablePhysicsAssets();
    const TArray<FAssetListItem>& GetAvailablePhysicsAssetFiles() const { return AvailablePhysicsAssetFiles; }

    void AddReferencedObjects(FReferenceCollector& Collector);

    static FString GetPhysicsAssetPackagePath(const FString& SkeletalMeshPackagePath);

private:
    FPhysicsAssetManager() = default;

    TMap<FString, UPhysicsAsset*> LoadedPhysicsAssets;
    TArray<FAssetListItem> AvailablePhysicsAssetFiles;
};
