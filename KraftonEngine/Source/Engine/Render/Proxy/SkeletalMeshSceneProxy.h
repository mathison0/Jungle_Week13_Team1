#pragma once

#include "Render/Proxy/ShapeSceneProxy.h"
#include "Render/Types/VertexTypes.h"

class USkeletalMeshComponent;
struct FDrawCommandBuffer;
struct FFrameContext;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent);
	~FSkeletalMeshSceneProxy() override;

	void UpdateMaterial() override;
	void UpdateMesh() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;

	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const override;
	bool PrepareGpuSkinningDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const;
	ID3D11ShaderResourceView* GetSkinMatrixSRV(ID3D11Device* Device, ID3D11DeviceContext* Context) const;

	const TArray<FWireLine>& GetCachedPhysicsAssetLines() const { return CachedPhysicsAssetLines; }
	const TArray<FVertex>& GetCachedPhysicsAssetSolidVertices() const { return CachedPhysicsAssetSolidVertices; }
	const TArray<uint32>& GetCachedPhysicsAssetSolidIndices() const { return CachedPhysicsAssetSolidIndices; }
	const TArray<FVertex>& GetCachedPhysicsConstraintSolidVertices() const { return CachedPhysicsConstraintSolidVertices; }
	const TArray<uint32>& GetCachedPhysicsConstraintSolidIndices() const { return CachedPhysicsConstraintSolidIndices; }
	const FVector4& GetPhysicsAssetColor() const { return PhysicsAssetColor; }
	
private:
	void RebuildSectionDraws();
	void RebuildPhysicsAssetDebugGeometry(const FFrameContext& Frame);
	USkeletalMeshComponent* GetSkeletalMeshComponent() const;
	void ReleaseSkinMatrixBuffer() const;
	bool UpdateSkinMatrixBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) const;

private:
	mutable FDynamicVertexBuffer DynamicVertexBuffer;
	mutable uint64 UploadedSkinnedRevision = 0;
	uint32 CachedDynamicVertexCount = 0;
	mutable bool bDynamicBufferNeedsCreate = true;

	mutable ID3D11Buffer* SkinMatrixBuffer = nullptr;
	mutable ID3D11ShaderResourceView* SkinMatrixSRV = nullptr;
	mutable uint32 SkinMatrixCapacity = 0;
	mutable uint64 UploadedSkinMatrixRevision = 0;

	TArray<FWireLine> CachedPhysicsAssetLines;
	TArray<FVertex> CachedPhysicsAssetSolidVertices;
	TArray<uint32> CachedPhysicsAssetSolidIndices;
	TArray<FVertex> CachedPhysicsConstraintSolidVertices;
	TArray<uint32> CachedPhysicsConstraintSolidIndices;
	FVector4 PhysicsAssetColor = FVector4(0.18f, 0.62f, 1.0f, 0.5f);
};
