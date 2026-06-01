#pragma once

#include "Object/Object.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Animation/Skeleton/SkeletonTypes.h"
#include "Object/Ptr/SoftObjectPtr.h"
#include "Physics/PhysicsAsset.h"

class USkeleton;
class FReferenceCollector;


#include "Source/Engine/Mesh/Skeletal/SkeletalMesh.generated.h"

UCLASS()
class USkeletalMesh : public UObject
{
public:
	GENERATED_BODY()
	USkeletalMesh() = default;
	~USkeletalMesh() override = default;

    void Serialize(FArchive& Ar) override;

    const FString& GetAssetPathFileName() const
    {
        return AssetPathFileName;
    }

    void SetAssetPathFileName(const FString& InPathFileName)
    {
        AssetPathFileName = InPathFileName;
    }

    void                             SetSkeletalMeshAsset(FSkeletalMesh* InMesh);
    FSkeletalMesh*                   GetSkeletalMeshAsset() const;
    void                             SetSkeletalMaterials(TArray<FSkeletalMaterial>&& InMaterials);
    const TArray<FSkeletalMaterial>& GetSkeletalMaterials() const;

    void InitResources(ID3D11Device* InDevice);

    void       SetSkeleton(USkeleton* InSkeleton);
    USkeleton* GetSkeleton() const;

    void SetSkeletonBinding(const FSkeletonBinding& InBinding);
    const FSkeletonBinding& GetSkeletonBinding() const { return SkeletonBinding; }

    UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }
    UPhysicsAsset* EnsurePhysicsAsset();
    bool GenerateDefaultPhysicsAsset(bool bOverwriteExisting = false);
    UBodySetup* AddDefaultPhysicsBodyForBone(int32 BoneIndex);
    bool AddPhysicsConstraintBetweenBodies(const FName& ParentBoneName, const FName& ChildBoneName);
    bool HasPhysicsConstraintBetweenBodies(const FName& BoneNameA, const FName& BoneNameB) const;
    void SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset);
    const FString& GetPhysicsAssetPath() const { return PhysicsAssetPath.ToString(); }

    void AddReferencedObjects(FReferenceCollector& Collector) override;
    void PostEditProperty(const char* PropertyName) override;

private:
    void LoadPhysicsAssetFromPath();
    void CacheSectionMaterialIndices();
    void SyncSkeletonBindingToAsset();
    void SyncSkeletonBindingFromAsset();

private:
    FString AssetPathFileName = "None";

    FSkeletalMesh*            SkeletalMeshAsset = nullptr;
    TArray<FSkeletalMaterial> SkeletalMaterials;

    FSkeletonBinding SkeletonBinding;
    USkeleton*       Skeleton = nullptr;
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Physics Asset", AssetType="UPhysicsAsset", AllowedClass=UPhysicsAsset)
    FSoftObjectPtr PhysicsAssetPath = "None";
    UPhysicsAsset* PhysicsAsset = nullptr;
};
