#include "Physics/PhysicsMaterial/PhysicalMaterialManager.h"

#include "Physics/PhysicsMaterial/PhysicalMaterial.h"
#include "Physics/PhysX/PhysXCore.h"
#include "Asset/AssetPackage.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"
#include "Object/ReferenceCollector.h"

#include <algorithm>
#include <filesystem>

UPhysicalMaterial* FPhysicalMaterialManager::Load(const FString& Path)
{
	// PhysX 코어 teardown 시 캐시 머티리얼 핸들을 무효화하도록 1회 등록.
	// (캡처 없는 람다라 함수 포인터로 변환된다.)
	static bool bTeardownHookRegistered = false;
	if (!bTeardownHookRegistered)
	{
		FPhysXCore::RegisterTeardownCallback([]()
		{
			FPhysicalMaterialManager::Get().InvalidateAllPxMaterials();
		});
		bTeardownHookRegistered = true;
	}

	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedMaterials.find(NormalizedPath);
	if (It != LoadedMaterials.end())
	{
		return It->second;
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath)) return nullptr;

	FWindowsBinReader Ar(NormalizedPath);
	if (!Ar.IsValid()) return nullptr;

	FAssetPackageHeader Header;
	Ar << Header;
	if (!Header.IsValid(EAssetPackageType::PhysicalMaterial)) return nullptr;

	FAssetImportMetadata Metadata;
	Ar << Metadata;

	UPhysicalMaterial* NewAsset = UObjectManager::Get().CreateObject<UPhysicalMaterial>();
	NewAsset->Serialize(Ar);

	if (!Ar.IsValid())
	{
		UObjectManager::Get().DestroyObject(NewAsset);
		return nullptr;
	}

	NewAsset->SetSourcePath(NormalizedPath);
	LoadedMaterials.emplace(NormalizedPath, NewAsset);
	return NewAsset;
}

UPhysicalMaterial* FPhysicalMaterialManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedMaterials.find(NormalizedPath);
	return It != LoadedMaterials.end() ? It->second : nullptr;
}

bool FPhysicalMaterialManager::Save(UPhysicalMaterial* Asset)
{
	if (!Asset) return false;

	const FString& Path = Asset->GetSourcePath();
	if (Path.empty()) return false;

	FWindowsBinWriter Ar(FPaths::MakeProjectRelative(Path));
	if (!Ar.IsValid()) return false;

	FAssetPackageHeader Header;
	Header.Type = static_cast<uint32>(EAssetPackageType::PhysicalMaterial);

	FAssetImportMetadata Metadata;

	Ar << Header;
	Ar << Metadata;
	Asset->Serialize(Ar);

	if (Ar.IsValid())
	{
		RefreshAvailablePhysicalMaterials();
	}

	return Ar.IsValid();
}

void FPhysicalMaterialManager::RefreshAvailablePhysicalMaterials()
{
	namespace fs = std::filesystem;

	const fs::path ContentRoot = fs::path(FPaths::RootDir()) / L"Content";
	if (!fs::exists(ContentRoot))
	{
		return;
	}

	const fs::path ProjectRoot(FPaths::RootDir());
	AvailablePhysicalMaterialFiles.clear();

	for (const auto& Entry : fs::recursive_directory_iterator(ContentRoot))
	{
		if (!Entry.is_regular_file())
		{
			continue;
		}

		std::wstring Ext = Entry.path().extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		if (Ext != L".uasset")
		{
			continue;
		}

		const FString RelPath =
			FPaths::ToUtf8(Entry.path().lexically_relative(ProjectRoot).generic_wstring());

		// PhysicalMaterial 타입 .uasset만 후보로. (헤더 Type 검증)
		FAssetImportMetadata Metadata;
		if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::PhysicalMaterial, Metadata))
		{
			continue;
		}

		FAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
		Item.FullPath = RelPath;
		AvailablePhysicalMaterialFiles.push_back(std::move(Item));
	}
}

void FPhysicalMaterialManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& [Path, Material] : LoadedMaterials)
	{
		Collector.AddReferencedObject(Material);
	}
}

void FPhysicalMaterialManager::InvalidateAllPxMaterials()
{
	// Physics가 아직 살아있을 때(코어 teardown 직전) 호출된다 → 각 PxMaterial을 안전하게 release하고
	// 핸들을 null로 비운다. 다음 Physics에서 GetOrCreatePxMaterial이 새로 생성하므로 dangling이 사라진다.
	for (auto& [Path, Material] : LoadedMaterials)
	{
		if (Material)
		{
			Material->ReleasePxMaterial();
		}
	}
}
