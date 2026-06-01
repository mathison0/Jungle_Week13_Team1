#include "Physics/PhysicsAssetManager.h"

#include "Asset/AssetPackage.h"
#include "Object/Object.h"
#include "Object/ReferenceCollector.h"
#include "Physics/PhysicsAsset.h"
#include "Platform/Paths.h"
#include "Serialization/WindowsArchive.h"

#include <algorithm>
#include <filesystem>

namespace
{
    FAssetImportMetadata MakeImportMetadata(const FString& SourcePath)
    {
        FAssetImportMetadata Metadata;
        if (SourcePath.empty())
        {
            return Metadata;
        }

        Metadata.SourcePath = FPaths::MakeProjectRelative(SourcePath);

        std::filesystem::path FullPath(FPaths::ToWide(SourcePath));
        if (!FullPath.is_absolute())
        {
            FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
        }
        FullPath = FullPath.lexically_normal();

        if (std::filesystem::exists(FullPath) && std::filesystem::is_regular_file(FullPath))
        {
            Metadata.SourceFileSize = static_cast<uint64>(std::filesystem::file_size(FullPath));
            Metadata.SourceTimestamp = static_cast<uint64>(
                std::filesystem::last_write_time(FullPath).time_since_epoch().count());
        }

        return Metadata;
    }
}

UPhysicsAsset* FPhysicsAssetManager::Load(const FString& Path)
{
    const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

    auto It = LoadedPhysicsAssets.find(NormalizedPath);
    if (It != LoadedPhysicsAssets.end())
    {
        return It->second;
    }

    if (!FAssetPackage::IsAssetPackagePath(NormalizedPath))
    {
        return nullptr;
    }

    FWindowsBinReader Ar(NormalizedPath);
    if (!Ar.IsValid())
    {
        return nullptr;
    }

    FAssetPackageHeader Header;
    Ar << Header;
    if (!Header.IsValid(EAssetPackageType::PhysicsAsset))
    {
        return nullptr;
    }

    FAssetImportMetadata Metadata;
    Ar << Metadata;

    UPhysicsAsset* Asset = UObjectManager::Get().CreateObject<UPhysicsAsset>();
    Asset->Serialize(Ar);
    if (!Ar.IsValid())
    {
        UObjectManager::Get().DestroyObject(Asset);
        return nullptr;
    }

    Asset->SetAssetPathFileName(NormalizedPath);
    LoadedPhysicsAssets[NormalizedPath] = Asset;
    return Asset;
}

UPhysicsAsset* FPhysicsAssetManager::Find(const FString& Path) const
{
    const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
    auto It = LoadedPhysicsAssets.find(NormalizedPath);
    return It != LoadedPhysicsAssets.end() ? It->second : nullptr;
}

bool FPhysicsAssetManager::Save(UPhysicsAsset* Asset, const FString& SourcePath)
{
    if (!Asset)
    {
        return false;
    }

    const FString NormalizedPath = FPaths::MakeProjectRelative(Asset->GetAssetPathFileName());
    if (NormalizedPath.empty() || NormalizedPath == "None")
    {
        return false;
    }

    FWindowsBinWriter Ar(NormalizedPath);
    if (!Ar.IsValid())
    {
        return false;
    }

    FAssetPackageHeader Header;
    Header.Type = static_cast<uint32>(EAssetPackageType::PhysicsAsset);

    FAssetImportMetadata Metadata = MakeImportMetadata(SourcePath);

    Ar << Header;
    Ar << Metadata;
    Asset->Serialize(Ar);

    if (!Ar.IsValid())
    {
        return false;
    }

    Asset->SetAssetPathFileName(NormalizedPath);
    LoadedPhysicsAssets[NormalizedPath] = Asset;
    RefreshAvailablePhysicsAssets();
    return true;
}

void FPhysicsAssetManager::RefreshAvailablePhysicsAssets()
{
    namespace fs = std::filesystem;

    const fs::path ContentRoot = fs::path(FPaths::RootDir()) / L"Content";
    if (!fs::exists(ContentRoot))
    {
        return;
    }

    const fs::path ProjectRoot(FPaths::RootDir());
    AvailablePhysicsAssetFiles.clear();

    for (const auto& Entry : fs::recursive_directory_iterator(ContentRoot))
    {
        if (!Entry.is_regular_file() || Entry.path().extension() != L".uasset")
        {
            continue;
        }

        const FString RelPath =
            FPaths::ToUtf8(Entry.path().lexically_relative(ProjectRoot).generic_wstring());

        FAssetImportMetadata Metadata;
        if (!FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::PhysicsAsset, Metadata))
        {
            continue;
        }

        FAssetListItem Item;
        Item.DisplayName = FPaths::ToUtf8(Entry.path().stem().wstring());
        Item.FullPath = RelPath;
        AvailablePhysicsAssetFiles.push_back(std::move(Item));
    }
}

void FPhysicsAssetManager::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (auto& [Path, Asset] : LoadedPhysicsAssets)
    {
        Collector.AddReferencedObject(Asset);
    }
}

FString FPhysicsAssetManager::GetPhysicsAssetPackagePath(const FString& SkeletalMeshPackagePath)
{
    std::filesystem::path AssetPath(FPaths::ToWide(FPaths::MakeProjectRelative(SkeletalMeshPackagePath)));
    std::wstring Stem = AssetPath.stem().wstring();
    static constexpr const wchar_t* SkeletalMeshSuffix = L"_SkeletalMesh";

    if (Stem.size() >= std::char_traits<wchar_t>::length(SkeletalMeshSuffix)
        && Stem.compare(Stem.size() - std::char_traits<wchar_t>::length(SkeletalMeshSuffix),
            std::char_traits<wchar_t>::length(SkeletalMeshSuffix),
            SkeletalMeshSuffix) == 0)
    {
        Stem.resize(Stem.size() - std::char_traits<wchar_t>::length(SkeletalMeshSuffix));
    }

    AssetPath.replace_filename(Stem + L"_PhysicsAsset.uasset");

    const std::filesystem::path FullAssetPath = std::filesystem::path(FPaths::RootDir()) / AssetPath;
    FPaths::CreateDir(FullAssetPath.parent_path().wstring());

    return FPaths::ToUtf8(AssetPath.generic_wstring());
}
