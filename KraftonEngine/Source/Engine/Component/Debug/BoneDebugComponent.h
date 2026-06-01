#pragma once

#include "Component/PrimitiveComponent.h"

#include "Source/Engine/Component/Debug/BoneDebugComponent.generated.h"
class USkeletalMeshComponent;
class UBodySetup;
class FScene;

enum class EBoneDebugDrawMode : uint8
{
	SelectedOnly,
	AllBones
};

UCLASS()
class UBoneDebugComponent : public UPrimitiveComponent
{
public:
	GENERATED_BODY()
	UBoneDebugComponent();
	~UBoneDebugComponent() override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;

	USkeletalMeshComponent* GetTargetMeshComponent() const { return TargetMeshComponent; }
	void SetTargetMeshComponent(USkeletalMeshComponent* InMeshComponent) { TargetMeshComponent = InMeshComponent; MarkRenderStateDirty(); }

	int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }
	void SetSelectedBoneIndex(int32 InBoneIndex) { SelectedBoneIndex = InBoneIndex; MarkRenderStateDirty(); }

	EBoneDebugDrawMode GetDrawMode() const { return DrawMode; }
	void SetDrawMode(EBoneDebugDrawMode InDrawMode) { DrawMode = InDrawMode; MarkRenderStateDirty(); }

	bool ShouldDrawPhysicsAsset() const { return bDrawPhysicsAsset; }
	void SetDrawPhysicsAsset(bool bInDrawPhysicsAsset)
	{
		if (bDrawPhysicsAsset == bInDrawPhysicsAsset) return;
		bDrawPhysicsAsset = bInDrawPhysicsAsset;
		MarkRenderStateDirty();
	}

	bool ShouldDrawPhysicsAssetSolid() const { return bDrawPhysicsAssetSolid; }
	void SetDrawPhysicsAssetSolid(bool bInDrawPhysicsAssetSolid)
	{
		if (bDrawPhysicsAssetSolid == bInDrawPhysicsAssetSolid) return;
		bDrawPhysicsAssetSolid = bInDrawPhysicsAssetSolid;
		MarkRenderStateDirty();
	}

	UBodySetup* GetSelectedPhysicsBodySetup() const { return SelectedPhysicsBodySetup; }
	void SetSelectedPhysicsBodySetup(UBodySetup* InBodySetup)
	{
		if (SelectedPhysicsBodySetup == InBodySetup) return;
		SelectedPhysicsBodySetup = InBodySetup;
		MarkRenderStateDirty();
	}

private:
	USkeletalMeshComponent* TargetMeshComponent = nullptr;
	UBodySetup* SelectedPhysicsBodySetup = nullptr;
	int32 SelectedBoneIndex = -1;
	EBoneDebugDrawMode DrawMode = EBoneDebugDrawMode::SelectedOnly;
	bool bDrawPhysicsAsset = false;
	bool bDrawPhysicsAssetSolid = true;
};
