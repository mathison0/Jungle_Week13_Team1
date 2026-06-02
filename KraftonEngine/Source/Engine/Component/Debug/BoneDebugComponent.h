#pragma once

#include "Component/PrimitiveComponent.h"
#include "Render/Types/ViewTypes.h"

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

	EPhysicsAssetBodyShowMode GetPhysicsAssetBodyShowMode() const { return PhysicsAssetBodyShowMode; }
	void SetPhysicsAssetBodyShowMode(EPhysicsAssetBodyShowMode InMode)
	{
		if (PhysicsAssetBodyShowMode == InMode) return;
		PhysicsAssetBodyShowMode = InMode;
		MarkRenderStateDirty();
	}

	EPhysicsAssetConstraintShowMode GetPhysicsAssetConstraintShowMode() const { return PhysicsAssetConstraintShowMode; }
	void SetPhysicsAssetConstraintShowMode(EPhysicsAssetConstraintShowMode InMode)
	{
		if (PhysicsAssetConstraintShowMode == InMode) return;
		PhysicsAssetConstraintShowMode = InMode;
		MarkRenderStateDirty();
	}

	bool ShouldDrawPhysicsAssetSolid() const { return PhysicsAssetBodyShowMode == EPhysicsAssetBodyShowMode::Solid; }
	void SetDrawPhysicsAssetSolid(bool bInDrawPhysicsAssetSolid)
	{
		SetPhysicsAssetBodyShowMode(bInDrawPhysicsAssetSolid
			? EPhysicsAssetBodyShowMode::Solid
			: EPhysicsAssetBodyShowMode::Wireframe);
	}

	UBodySetup* GetSelectedPhysicsBodySetup() const { return SelectedPhysicsBodySetup; }
	void SetSelectedPhysicsBodySetup(UBodySetup* InBodySetup)
	{
		if (SelectedPhysicsBodySetup == InBodySetup) return;
		SelectedPhysicsBodySetup = InBodySetup;
		MarkRenderStateDirty();
	}

	int32 GetSelectedPhysicsConstraintIndex() const { return SelectedPhysicsConstraintIndex; }
	void SetSelectedPhysicsConstraintIndex(int32 InConstraintIndex)
	{
		if (SelectedPhysicsConstraintIndex == InConstraintIndex) return;
		SelectedPhysicsConstraintIndex = InConstraintIndex;
		MarkRenderStateDirty();
	}

private:
	USkeletalMeshComponent* TargetMeshComponent = nullptr;
	UBodySetup* SelectedPhysicsBodySetup = nullptr;
	int32 SelectedPhysicsConstraintIndex = -1;
	int32 SelectedBoneIndex = -1;
	EBoneDebugDrawMode DrawMode = EBoneDebugDrawMode::SelectedOnly;
	bool bDrawPhysicsAsset = false;
	EPhysicsAssetBodyShowMode PhysicsAssetBodyShowMode = EPhysicsAssetBodyShowMode::Solid;
	EPhysicsAssetConstraintShowMode PhysicsAssetConstraintShowMode = EPhysicsAssetConstraintShowMode::Solid;
};
