#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Proxy/ShapeSceneProxy.h"

class UStaticMeshComponent;

// ============================================================
// FStaticMeshSceneProxy — UStaticMeshComponent 전용 프록시
// ============================================================
// StaticMesh의 섹션별 머티리얼, 메시 버퍼, 셰이더를 캐싱.
// Mesh/Material dirty 시 SectionDraws를 재구축한다.
// LOD: 거리 기반으로 MeshBuffer + SectionDraws를 스왑.
class FStaticMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	static constexpr uint32 MAX_LOD = 4;

	FStaticMeshSceneProxy(UStaticMeshComponent* InComponent);

	void UpdateTransform() override;
	void UpdateMaterial() override;
	void UpdateMesh() override;
	void UpdateLOD(uint32 LODLevel) override;

	const TArray<FWireLine>& GetCachedTriangleCollisionLines() const { return CachedTriangleCollisionLines; }
	const FVector4& GetTriangleCollisionColor() const { return TriangleCollisionColor; }

private:
	UStaticMeshComponent* GetStaticMeshComponent() const;

	// 모든 LOD의 SectionDraws 재구축
	void RebuildSectionDraws();
	void RebuildTriangleCollisionLines();

	struct FLODDrawData
	{
		FMeshBuffer* MeshBuffer = nullptr;
		TArray<FMeshSectionDraw> SectionDraws;
	};

	FLODDrawData LODData[MAX_LOD];
	uint32 LODCount = 1;

	TArray<FWireLine> CachedTriangleCollisionLines;
	FVector4 TriangleCollisionColor = { 0.235f, 0.863f, 1.0f, 0.863f };
};
