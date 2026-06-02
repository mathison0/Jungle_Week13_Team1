#include "Render/Proxy/ClothSceneProxy.h"

#include "Component/Primitive/ClothComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Shader/ShaderManager.h"

FClothSceneProxy::FClothSceneProxy(UClothComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::Cloth;
}

FClothSceneProxy::~FClothSceneProxy()
{
	DefaultClothMaterialCB.Release();
}

UClothComponent* FClothSceneProxy::GetClothComponent() const
{
	return static_cast<UClothComponent*>(GetOwner());
}

void FClothSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
}

void FClothSceneProxy::UpdateVisibility()
{
	FPrimitiveSceneProxy::UpdateVisibility();
}

void FClothSceneProxy::UpdateMesh()
{
	MeshBuffer = nullptr;
	RebuildSectionDraws();
}

bool FClothSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	UClothComponent* Component = GetClothComponent();
	if (!Component)
	{
		return false;
	}

	const FMeshDataView View = Component->GetMeshDataView();
	if (!View.IsValid() || View.Stride != sizeof(FVertexPNCTT) || !Device || !Context)
	{
		return false;
	}

	if (VertexBuffer.GetMaxCount() == 0 || VertexBuffer.GetStride() != sizeof(FVertexPNCTT))
	{
		if (!VertexBuffer.Create(Device, View.VertexCount, sizeof(FVertexPNCTT)))
		{
			return false;
		}
	}
	else
	{
		if (!VertexBuffer.EnsureCapacity(Device, View.VertexCount))
		{
			return false;
		}
	}

	if (IndexBuffer.GetMaxCount() == 0)
	{
		if (!IndexBuffer.Create(Device, View.IndexCount))
		{
			return false;
		}
	}
	else
	{
		if (!IndexBuffer.EnsureCapacity(Device, View.IndexCount))
		{
			return false;
		}
	}

	if (!VertexBuffer.Update(Context, View.VertexData, View.VertexCount))
	{
		return false;
	}

	if (!IndexBuffer.Update(Context, View.IndexData, View.IndexCount))
	{
		return false;
	}

	OutBuffer = {};
	OutBuffer.VB = VertexBuffer.GetBuffer();
	OutBuffer.VBStride = VertexBuffer.GetStride();
	OutBuffer.IB = IndexBuffer.GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

void FClothSceneProxy::RebuildSectionDraws()
{
	UClothComponent* Component = GetClothComponent();
	if (!Component)
	{
		SectionDraws.clear();
		return;
	}

	const FMeshDataView View = Component->GetMeshDataView();
	SectionDraws.clear();

	if (!View.IsValid())
	{
		return;
	}

	UMaterialInterface* Material = Component->GetMaterial();
	if (!Material)
	{
		if (!DefaultMaterial)
		{
			DefaultMaterial = FMaterialManager::Get().CreateTransientMaterial(
				ERenderPass::Opaque,
				EBlendState::Opaque,
				EDepthStencilState::Default,
				ERasterizerState::SolidBackCull,
				FShaderManager::Get().GetOrCreate(EShaderPath::UberLit));
		}
		Material = DefaultMaterial;
	}

	if (Material == DefaultMaterial && DefaultMaterial)
	{
		FDefaultClothMaterialConstants& Constants = DefaultMaterial->BindPerShaderCB<FDefaultClothMaterialConstants>(
			&DefaultClothMaterialCB,
			ECBSlot::PerShader0);
		Constants = FDefaultClothMaterialConstants();
	}

	FMeshSectionDraw Draw;
	Draw.Material = Material;
	Draw.FirstIndex = 0;
	Draw.IndexCount = View.IndexCount;
	SectionDraws.push_back(Draw);
}
