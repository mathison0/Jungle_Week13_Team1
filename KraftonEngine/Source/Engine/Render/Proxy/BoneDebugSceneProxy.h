#pragma once

#include "ShapeSceneProxy.h"
#include "Render/Types/ViewTypes.h"
#include "Render/Types/VertexTypes.h"

class UBoneDebugComponent;
class USkeletalMeshComponent;
struct FSkeletalMesh;

class FBoneDebugSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FBoneDebugSceneProxy(UBoneDebugComponent* InComponent);
	~FBoneDebugSceneProxy() override;

	void UpdateTransform() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

	const TArray<FWireLine>& GetCachedLines() const { return CachedLines; }
	const TArray<FWireLine>& GetCachedParentBoneLines() const { return CachedParentBoneLines; }
	const TArray<FWireLine>& GetCachedPhysicsAssetLines() const { return CachedPhysicsAssetLines; }
	const TArray<FVertex>& GetCachedPhysicsAssetSolidVertices() const { return CachedPhysicsAssetSolidVertices; }
	const TArray<uint32>& GetCachedPhysicsAssetSolidIndices() const { return CachedPhysicsAssetSolidIndices; }
	const TArray<FVertex>& GetCachedPhysicsConstraintSolidVertices() const { return CachedPhysicsConstraintSolidVertices; }
	const TArray<uint32>& GetCachedPhysicsConstraintSolidIndices() const { return CachedPhysicsConstraintSolidIndices; }

	const FVector4& GetBoneColor() const { return BoneColor; }
	const FVector4& GetParentBoneColor() const { return ParentBoneColor; }
	const FVector4& GetPhysicsAssetColor() const { return PhysicsAssetColor; }

private:
	void RebuildLines();
	void RebuildPhysicsAssetLines(UBoneDebugComponent* Comp, USkeletalMeshComponent* MeshComp, const FSkeletalMesh* Asset);

private:
	TArray<FWireLine> CachedLines;
	TArray<FWireLine> CachedParentBoneLines;
	TArray<FWireLine> CachedPhysicsAssetLines;
	TArray<FVertex> CachedPhysicsAssetSolidVertices;
	TArray<uint32> CachedPhysicsAssetSolidIndices;
	TArray<FVertex> CachedPhysicsConstraintSolidVertices;
	TArray<uint32> CachedPhysicsConstraintSolidIndices;

	EPhysicsAssetBodyShowMode ViewportPhysicsAssetBodyShowMode = EPhysicsAssetBodyShowMode::Solid;
	EPhysicsAssetConstraintShowMode ViewportPhysicsAssetConstraintShowMode = EPhysicsAssetConstraintShowMode::Solid;

	FVector4 BoneColor;
	FVector4 ParentBoneColor;
	FVector4 PhysicsAssetColor;
};
