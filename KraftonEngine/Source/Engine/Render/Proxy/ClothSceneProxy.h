#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"

class UClothComponent;

class FClothSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FClothSceneProxy(UClothComponent* InComponent);
	~FClothSceneProxy() override;

	void UpdateMaterial() override;
	void UpdateVisibility() override;
	void UpdateMesh() override;

	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
		FDrawCommandBuffer& OutBuffer) const override;

private:
	UClothComponent* GetClothComponent() const;
	void RebuildSectionDraws();

	mutable FDynamicVertexBuffer VertexBuffer;
	mutable FDynamicIndexBuffer IndexBuffer;
	mutable FConstantBuffer DefaultClothMaterialCB;
};
