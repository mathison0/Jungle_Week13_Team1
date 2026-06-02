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
	struct FDefaultClothMaterialConstants
	{
		FVector4 SectionColor = FVector4(0.78f, 0.82f, 0.90f, 1.0f);
		float HasNormalMap = 0.0f;
		float Padding[3] = { 0.0f, 0.0f, 0.0f };
	};

	UClothComponent* GetClothComponent() const;
	void RebuildSectionDraws();

	mutable FDynamicVertexBuffer VertexBuffer;
	mutable FDynamicIndexBuffer IndexBuffer;
	mutable FConstantBuffer DefaultClothMaterialCB;
};
