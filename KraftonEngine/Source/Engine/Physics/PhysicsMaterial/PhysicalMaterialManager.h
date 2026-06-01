#pragma once

#include "Asset/AssetRegistry.h"
#include "Core/Singleton.h"
#include "Core/Types/CoreTypes.h"

class UPhysicalMaterial;
class FReferenceCollector;

// ===========================
// FPhysicalMaterialManager
// - UPhysicalMaterial 에셋(.uasset)의 저장/로드/캐시 + Content 폴더 스캔 담당.
// ===========================
class FPhysicalMaterialManager : public TSingleton<FPhysicalMaterialManager>
{
	friend class TSingleton<FPhysicalMaterialManager>;

public:
	UPhysicalMaterial* Load(const FString& Path);
	UPhysicalMaterial* Find(const FString& Path) const;

	bool Save(UPhysicalMaterial* Asset);

	// 콤보(피커) 열 때마다 재스캔 — 방금 만든 에셋도 즉시 노출되도록.
	void RefreshAvailablePhysicalMaterials();
	const TArray<FAssetListItem>& GetAvailablePhysicalMaterialFiles() const { return AvailablePhysicalMaterialFiles; }

	// GC: 로드해 캐시한 머티리얼을 루트로 등록해 수거되지 않게 한다 (GarbageCollector가 호출).
	void AddReferencedObjects(FReferenceCollector& Collector);

private:
	FPhysicalMaterialManager() = default;

	TMap<FString, UPhysicalMaterial*> LoadedMaterials;
	TArray<FAssetListItem> AvailablePhysicalMaterialFiles;
};
