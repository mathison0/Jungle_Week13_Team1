#pragma once

#include "Object/Object.h"
#include "Physics/BodySetup.h"
#include "Physics/ConstraintInstance.h"

#include "Source/Engine/Physics/PhysicsAsset.generated.h"

struct FSkeletalMesh;

struct FPhysicsAssetAutoGenerateSettings
{
    bool bReplaceExisting = true;
    bool bCreateConstraints = true;
    bool bUseDominantBoneOnly = true;
    bool bUseDefaultNameFilters = true;

    float MinBoneWeight = 0.25f;
    float LowerPercentile = 0.05f;
    float UpperPercentile = 0.95f;
    float ShapePadding = 1.10f;
    float MinShapeSize = 0.01f;
    int32 MinVertexCount = 8;

    // [merge-up] 작은/깊은 본을 드롭하지 않고 부모 바디로 병합해 바디 폭발을 막는 임계값(에디터에서 튜닝).
    float MinBoneSizeRatio = 0.04f; // 본 스킨버텍스 AABB 최대 extent / 메시 전체 extent. 미만이면 부모로 병합.
    int32 MaxBoneDepth = 0;         // 루트로부터 깊이 초과 본을 부모로 병합. 0=무제한.
    int32 MaxBodyCount = 0;         // 생성 바디 수 안전캡. 초과분은 볼륨 작은 순으로 병합. 0=무제한.
};

struct FPhysicsAssetAutoGenerateStats
{
    int32 BodyCount = 0;
    int32 ConstraintCount = 0;
    int32 SkippedBoneCount = 0;
};

UCLASS()
class UPhysicsAsset : public UObject
{
public:
    GENERATED_BODY()

    UPhysicsAsset() = default;
    ~UPhysicsAsset() override = default;

    void Serialize(FArchive& Ar) override;

    void SetAssetPathFileName(const FString& InPath) { AssetPathFileName = InPath; }
    const FString& GetAssetPathFileName() const { return AssetPathFileName; }

    // PhysicsAsset Editor에서 bone별 collision body를 편집하는 목록.
    // 각 BodySetup의 BoneName은 skeleton에 실제로 존재해야 하며, bone 하나당 하나를 권장한다.
    UPROPERTY(Edit, Save, Instanced, Category="Physics", DisplayName="Body Setups", Type=Array)
    TArray<UBodySetup*> BodySetups;

    // 두 body를 연결하는 ragdoll joint 설정 목록.
    // ParentBoneName과 ChildBoneName은 위 BodySetups에 등록된 bone 이름을 가리켜야 한다.
    UPROPERTY(Edit, Save, Category="Physics", DisplayName="Constraint Setups", Type=Array, Struct=FConstraintSetup)
    TArray<FConstraintSetup> ConstraintSetups;

    bool HasAnyBodySetup() const;
    bool HasAnyConstraintSetup() const;
    int32 FindBodySetupIndexByBoneName(const FName& BoneName) const;
    UBodySetup* FindBodySetupByBoneName(const FName& BoneName) const;

    bool AutoGeneratePrimitiveBodiesFromSkeletalMesh(
        const FSkeletalMesh& Mesh,
        const FPhysicsAssetAutoGenerateSettings& Settings = FPhysicsAssetAutoGenerateSettings(),
        FPhysicsAssetAutoGenerateStats* OutStats = nullptr);

private:
    FString AssetPathFileName = "None";
};
