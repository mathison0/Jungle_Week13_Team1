#include "Render/Proxy/StaticMeshSceneProxy.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Materials/Material.h"

#include <algorithm>
#include <unordered_set>

namespace
{
	bool SectionMaterialLess(const FMeshSectionDraw& A, const FMeshSectionDraw& B)
	{
		const uintptr_t AMat = reinterpret_cast<uintptr_t>(A.Material);
		const uintptr_t BMat = reinterpret_cast<uintptr_t>(B.Material);
		if (AMat != BMat)
			return AMat < BMat;

		return A.FirstIndex < B.FirstIndex;
	}

	void SortSectionDrawsByMaterial(TArray<FMeshSectionDraw>& Draws)
	{
		if (Draws.size() > 1)
		{
			std::sort(Draws.begin(), Draws.end(), SectionMaterialLess);
		}
	}
}

// ============================================================
// FStaticMeshSceneProxy
// ============================================================
FStaticMeshSceneProxy::FStaticMeshSceneProxy(UStaticMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::StaticMesh;
}

UStaticMeshComponent* FStaticMeshSceneProxy::GetStaticMeshComponent() const
{
	return static_cast<UStaticMeshComponent*>(GetOwner());
}

void FStaticMeshSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	RebuildTriangleCollisionLines();
}

// ============================================================
// UpdateMaterial — 머티리얼만 변경된 경우 SectionDraws 재구축
// ============================================================
void FStaticMeshSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
}

// ============================================================
// UpdateMesh — 메시 버퍼 + 셰이더 교체 후 SectionDraws 재구축
// ============================================================
void FStaticMeshSceneProxy::UpdateMesh()
{
	MeshBuffer = GetOwner()->GetMeshBuffer();
	RebuildSectionDraws();
	RebuildTriangleCollisionLines();
}

// ============================================================
// UpdateLOD — LOD 레벨 변경 시 MeshBuffer/SectionDraws 스왑
// ============================================================
void FStaticMeshSceneProxy::UpdateLOD(uint32 LODLevel)
{
	if (LODLevel >= LODCount) LODLevel = LODCount - 1;
	if (LODLevel == CurrentLOD) return;

	// 현재 활성 데이터를 LODData 슬롯에 swap (할당/해제 없는 O(1) 교환)
	std::swap(MeshBuffer, LODData[CurrentLOD].MeshBuffer);
	std::swap(SectionDraws, LODData[CurrentLOD].SectionDraws);

	// 새 LOD 데이터를 활성 슬롯에서 swap
	CurrentLOD = LODLevel;
	std::swap(MeshBuffer, LODData[LODLevel].MeshBuffer);
	std::swap(SectionDraws, LODData[LODLevel].SectionDraws);

}

// ============================================================
// RebuildSectionDraws — 모든 LOD의 SectionDraws 재구축
// ============================================================
void FStaticMeshSceneProxy::RebuildSectionDraws()
{
	UStaticMeshComponent* SMC = GetStaticMeshComponent();
	UStaticMesh* Mesh = SMC->GetStaticMesh();
	if (!Mesh || !Mesh->GetStaticMeshAsset())
	{
		for (uint32 lod = 0; lod < MAX_LOD; ++lod)
		{
			LODData[lod].MeshBuffer = nullptr;
			LODData[lod].SectionDraws.clear();
		}

		LODCount = 1;
		CurrentLOD = 0;
		MeshBuffer = nullptr;
		SectionDraws.clear();

		return;
	}

	const auto& Slots = Mesh->GetStaticMaterials();
	const auto& Overrides = SMC->GetOverrideMaterials();
	LODCount = Mesh->GetLODCount();

	// 각 LOD별 SectionDraws + MeshBuffer 구축
	for (uint32 lod = 0; lod < LODCount; ++lod)
	{
		const auto& Sections = Mesh->GetLODSections(lod);
		LODData[lod].MeshBuffer = Mesh->GetLODMeshBuffer(lod);
		LODData[lod].SectionDraws.clear();
		LODData[lod].SectionDraws.reserve(Sections.size());

		for (const FStaticMeshSection& Section : Sections)
		{
			FMeshSectionDraw Draw;
			Draw.FirstIndex = Section.FirstIndex;
			Draw.IndexCount = Section.NumTriangles * 3;

			int32 i = Section.MaterialIndex;
			if (i >= 0 && i < static_cast<int32>(Slots.size()))
			{
				if (i < static_cast<int32>(Overrides.size()) && Overrides[i])
					Draw.Material = Overrides[i];
				else if (Slots[i].MaterialInterface)
					Draw.Material = Slots[i].MaterialInterface;
			}

			LODData[lod].SectionDraws.push_back(Draw);
		}

		SortSectionDrawsByMaterial(LODData[lod].SectionDraws);
	}

	// LOD0을 활성 슬롯으로 설정
	CurrentLOD = 0;
	std::swap(MeshBuffer, LODData[0].MeshBuffer);
	std::swap(SectionDraws, LODData[0].SectionDraws);

}

void FStaticMeshSceneProxy::RebuildTriangleCollisionLines()
{
	CachedTriangleCollisionLines.clear();

	UStaticMeshComponent* SMC = GetStaticMeshComponent();
	if (!SMC)
	{
		return;
	}

	UStaticMesh* StaticMesh = SMC->GetStaticMesh();
	if (!StaticMesh || !StaticMesh->IsTriangleMeshCollisionEnabled())
	{
		return;
	}

	const FStaticMesh* MeshAsset = StaticMesh->GetStaticMeshAsset();
	if (!MeshAsset || MeshAsset->Indices.size() < 3)
	{
		return;
	}

	const FMatrix& WorldMatrix = SMC->GetWorldMatrix();
	CachedTriangleCollisionLines.reserve(MeshAsset->Indices.size());

	std::unordered_set<uint64> UniqueEdges;
	UniqueEdges.reserve(MeshAsset->Indices.size());
	auto AddUniqueEdge = [&](uint32 IndexA, uint32 IndexB)
	{
		const uint32 MinIndex = IndexA < IndexB ? IndexA : IndexB;
		const uint32 MaxIndex = IndexA < IndexB ? IndexB : IndexA;
		const uint64 EdgeKey = (static_cast<uint64>(MinIndex) << 32) | static_cast<uint64>(MaxIndex);
		if (!UniqueEdges.insert(EdgeKey).second)
		{
			return;
		}

		CachedTriangleCollisionLines.push_back({
			WorldMatrix.TransformPositionWithW(MeshAsset->Vertices[IndexA].pos),
			WorldMatrix.TransformPositionWithW(MeshAsset->Vertices[IndexB].pos)
		});
	};

	for (size_t IndexOffset = 0; IndexOffset + 2 < MeshAsset->Indices.size(); IndexOffset += 3)
	{
		const uint32 I0 = MeshAsset->Indices[IndexOffset];
		const uint32 I1 = MeshAsset->Indices[IndexOffset + 1];
		const uint32 I2 = MeshAsset->Indices[IndexOffset + 2];
		if (I0 >= MeshAsset->Vertices.size() || I1 >= MeshAsset->Vertices.size() || I2 >= MeshAsset->Vertices.size())
		{
			continue;
		}

		AddUniqueEdge(I0, I1);
		AddUniqueEdge(I1, I2);
		AddUniqueEdge(I2, I0);
	}
}
