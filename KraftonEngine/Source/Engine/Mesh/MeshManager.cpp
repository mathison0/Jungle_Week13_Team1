#include "MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Importer/ObjImporter.h"
#include "Mesh/Importer/FbxImporter.h"
#include "Mesh/MeshBinary.h"
#include "Materials/Material.h"
#include "Core/Logging/Log.h"
#include "Serialization/WindowsArchive.h"
#include "Engine/Platform/Paths.h"
#include "Materials/MaterialManager.h"
#include "Asset/AssetPackage.h"

#include <algorithm>
#include <cwctype>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>

#include "Animation/AnimationManager.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Animation/Skeleton/SkeletonManager.h"
#include "Animation/Skeleton/SkeletonTypes.h"

#include "Object/ReferenceCollector.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"
#include "Physics/PhysX/PhysXCore.h"
#include "Physics/BodySetup.h"

#include <PxPhysicsAPI.h>

TMap<FString, UStaticMesh*> FMeshManager::StaticMeshCache;
TMap<FString, USkeletalMesh*> FMeshManager::SkeletalMeshCache;
TArray<FAssetListItem> FMeshManager::AvailableStaticMeshFiles;
TArray<FAssetListItem> FMeshManager::AvailableStaticMeshSourceFiles;
TArray<FAssetListItem> FMeshManager::AvailableSkeletalMeshFiles;
TArray<FAssetListItem> FMeshManager::AvailableFbxSourceFiles;

FMeshManager& FMeshManager::Get()
{
	static FMeshManager Instance;
	return Instance;
}

static void EnsureMeshCacheDirExists()
{
	static bool bCreated = false;
	if (!bCreated)
	{
		std::wstring CacheDir = FPaths::RootDir() + L"Content/MeshCache/";
		FPaths::CreateDir(CacheDir);
		bCreated = true;
	}
}

static std::wstring GetLowerExtension(const FString& Path)
{
	std::filesystem::path SrcPath(FPaths::ToWide(Path));
	std::wstring Ext = SrcPath.extension().wstring();
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
	return Ext;
}

static FString NormalizeProjectPath(const FString& Path)
{
	return FPaths::MakeProjectRelative(Path);
}

static FString GetMeshPackageFilePath(const FString& SourcePath, EAssetPackageType Type)
{
	std::filesystem::path SrcPath(FPaths::ToWide(SourcePath));
	std::wstring Ext = SrcPath.extension().wstring();
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);

	if (Ext == MeshBinary::AssetPackageExtension)
	{
		return NormalizeProjectPath(SourcePath);
	}

	// 경로는 동일하지만, 확장자를 나눠서 Mesh 타입을 알 수 있게 한다.
	// SourcePath 가 이미 Content/ 하위면 (cleanup 이후 모든 source) 그대로 사용 — 그렇지
	// 않으면 (구 Data/ root 호환) Content/ prefix 박음. prefix 이중 적용 방지.
	std::filesystem::path ProjectRelative = std::filesystem::path(FPaths::ToWide(FPaths::MakeProjectRelative(SourcePath))).lexically_normal();

	std::filesystem::path AssetPath;
	if (!ProjectRelative.empty() && ProjectRelative.begin()->wstring() == L"Content")
	{
		AssetPath = ProjectRelative;
	}
	else
	{
		AssetPath = std::filesystem::path(L"Content") / ProjectRelative;
	}

	if (Type == EAssetPackageType::StaticMesh)
	{
		AssetPath.replace_filename(AssetPath.stem().wstring() + L"_StaticMesh" + L".uasset");
	}
	else if (Type == EAssetPackageType::SkeletalMesh)
	{
		AssetPath.replace_filename(AssetPath.stem().wstring() + L"_SkeletalMesh" + L".uasset");
	}
	else
	{
		UE_LOG("GetMeshPackageFilePath failed: unsupported asset package type. SourcePath=%s", SourcePath.c_str());
		return FString();
	}

	std::filesystem::path FullAssetPath = std::filesystem::path(FPaths::RootDir()) / AssetPath;

	FPaths::CreateDir(FullAssetPath.parent_path().wstring());

	return FPaths::ToUtf8(AssetPath.generic_wstring());
}

static std::filesystem::path ResolveProjectPath(const FString& Path)
{
	std::filesystem::path FullPath(FPaths::ToWide(Path));
	if (!FullPath.is_absolute())
	{
		FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
	}
	return FullPath.lexically_normal();
}

static bool TryGetSourceFileState(const FString& SourcePath, uint64& OutTimestamp, uint64& OutFileSize)
{
	std::filesystem::path FullPath = ResolveProjectPath(SourcePath);

	if (!std::filesystem::exists(FullPath) || !std::filesystem::is_regular_file(FullPath)) return false;

	OutFileSize = static_cast<uint64>(std::filesystem::file_size(FullPath));

	const auto WriteTime = std::filesystem::last_write_time(FullPath);
	OutTimestamp = static_cast<uint64>(WriteTime.time_since_epoch().count());

	return true;
}

static FAssetImportMetadata MakeImportMetadata(const FString& SourcePath)
{
	FAssetImportMetadata Metadata;
	Metadata.SourcePath = NormalizeProjectPath(SourcePath);

	TryGetSourceFileState(SourcePath, Metadata.SourceTimestamp, Metadata.SourceFileSize);

	return Metadata;
}

static bool IsPackageSourceStale(const FString& BinaryPath, EAssetPackageType ExpectedType, bool& bOutMissingSource)
{
	bOutMissingSource = false;

	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadMetadata(BinaryPath, ExpectedType, Metadata)) return true;

	uint64 CurrentTimestamp = 0;
	uint64 CurrentFileSize = 0;
	if (!TryGetSourceFileState(Metadata.SourcePath, CurrentTimestamp, CurrentFileSize))
	{
		bOutMissingSource = true;
		return true;
	}

	return !Metadata.MatchesSource(CurrentTimestamp, CurrentFileSize);
}

static bool IsSupportedStaticMeshSourcePath(const FString& Path)
{
	const std::wstring Ext = GetLowerExtension(Path);
	return Ext == L".obj" || Ext == L".fbx";
}

static bool IsSupportedSkeletalMeshSourcePath(const FString& Path)
{
	const std::wstring Ext = GetLowerExtension(Path);
	return Ext == L".fbx";
}

static bool ImportStaticMeshByExtension(const FString& PathFileName, const FImportOptions* Options, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials)
{
	const std::wstring Ext = GetLowerExtension(PathFileName);
	if (Ext == L".obj")
	{
		return Options
			? FObjImporter::Import(PathFileName, *Options, OutMesh, OutMaterials)
			: FObjImporter::Import(PathFileName, OutMesh, OutMaterials);
	}

	if (Ext == L".fbx")
	{
		FFbxStaticMeshImportResult ImportResult;
		if (!FFbxImporter::ImportStaticMesh(PathFileName, Options, ImportResult))
		{
			return false;
		}

		OutMesh = std::move(ImportResult.Mesh);
		OutMaterials = std::move(ImportResult.Materials);
		return true;
	}

	UE_LOG("StaticMesh import failed: unsupported source extension. Path=%s", PathFileName.c_str());
	return false;
}

namespace
{
	struct FScopedPhysXCoreForCooking
	{
		physx::PxFoundation* Foundation = nullptr;
		physx::PxPhysics* Physics = nullptr;
		bool bAcquired = false;

#ifdef _DEBUG
		physx::PxPvd* Pvd = nullptr;
		physx::PxPvdTransport* PvdTransport = nullptr;
#endif

		FScopedPhysXCoreForCooking()
		{
#ifdef _DEBUG
			bAcquired = FPhysXCore::Acquire(Foundation, Physics, Pvd, PvdTransport);
#else
			bAcquired = FPhysXCore::Acquire(Foundation, Physics);
#endif
		}

		~FScopedPhysXCoreForCooking()
		{
			if (bAcquired)
			{
				FPhysXCore::Release();
			}
		}

		bool IsValid() const
		{
			return bAcquired && Foundation && Physics;
		}
	};

	const char* TriangleCookingResultToString(physx::PxTriangleMeshCookingResult::Enum Result)
	{
		switch (Result)
		{
		case physx::PxTriangleMeshCookingResult::eSUCCESS:
			return "Success";
		case physx::PxTriangleMeshCookingResult::eLARGE_TRIANGLE:
			return "Large triangle";
		case physx::PxTriangleMeshCookingResult::eFAILURE:
			return "Failure";
		default:
			return "Unknown";
		}
	}

	bool BuildStaticMeshTriangleCookingInput(const FStaticMesh& Mesh, TArray<FVector>& OutVertices, TArray<uint32>& OutIndices, FString* OutError)
	{
		auto SetError = [OutError](const char* Message)
		{
			if (OutError)
			{
				*OutError = Message ? Message : "";
			}
		};

		OutVertices.clear();
		OutIndices.clear();

		if (Mesh.Vertices.size() < 3 || Mesh.Indices.size() < 3 || (Mesh.Indices.size() % 3) != 0)
		{
			SetError("StaticMesh triangle collision input is incomplete.");
			return false;
		}

		// PhysX cooking desc는 point stride를 직접 받는다. FNormalVertex는 렌더 vertex라
		// position 외의 데이터가 섞여 있으므로, collision에 필요한 pos만 tight FVector 배열로 분리한다.
		OutVertices.reserve(Mesh.Vertices.size());
		for (const FNormalVertex& Vertex : Mesh.Vertices)
		{
			OutVertices.push_back(Vertex.pos);
		}

		OutIndices.reserve(Mesh.Indices.size());
		for (uint32 Index : Mesh.Indices)
		{
			if (Index >= Mesh.Vertices.size())
			{
				SetError("StaticMesh triangle collision index is out of range.");
				OutVertices.clear();
				OutIndices.clear();
				return false;
			}
			OutIndices.push_back(Index);
		}

		SetError("");
		return true;
	}

	bool CookTriangleMeshPhysX(const TArray<FVector>& Vertices, const TArray<uint32>& Indices, TArray<uint8>& OutCookedData, FString* OutError)
	{
		auto SetError = [OutError](const char* Message)
		{
			if (OutError)
			{
				*OutError = Message ? Message : "";
			}
		};

		OutCookedData.clear();

		FScopedPhysXCoreForCooking PhysXCore;
		if (!PhysXCore.IsValid())
		{
			SetError("PhysX Foundation/Physics could not be created for triangle mesh cooking.");
			return false;
		}

		if (Vertices.size() < 3 || Indices.size() < 3 || (Indices.size() % 3) != 0)
		{
			SetError("Triangle mesh input is incomplete.");
			return false;
		}

		for (uint32 Index : Indices)
		{
			if (Index >= Vertices.size())
			{
				SetError("Triangle mesh index is out of range.");
				return false;
			}
		}

		physx::PxCookingParams CookingParams(PhysXCore.Physics->getTolerancesScale());
		CookingParams.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eWELD_VERTICES;
		CookingParams.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eFORCE_32BIT_INDICES;
		CookingParams.meshWeldTolerance = 0.001f;

		physx::PxCooking* Cooking = PxCreateCooking(PX_PHYSICS_VERSION, *PhysXCore.Foundation, CookingParams);
		if (!Cooking)
		{
			SetError("PxCreateCooking failed.");
			return false;
		}

		physx::PxTriangleMeshDesc Desc;
		Desc.points.count = static_cast<physx::PxU32>(Vertices.size());
		Desc.points.stride = sizeof(FVector);
		Desc.points.data = Vertices.data();
		Desc.triangles.count = static_cast<physx::PxU32>(Indices.size() / 3);
		Desc.triangles.stride = sizeof(uint32) * 3;
		Desc.triangles.data = Indices.data();

		if (!Desc.isValid())
		{
			Cooking->release();
			SetError("PxTriangleMeshDesc is invalid.");
			return false;
		}

		physx::PxTriangleMeshCookingResult::Enum CookResult = physx::PxTriangleMeshCookingResult::eFAILURE;
		physx::PxDefaultMemoryOutputStream OutputStream;
		const bool bCooked = Cooking->cookTriangleMesh(Desc, OutputStream, &CookResult);
		if (bCooked && CookResult != physx::PxTriangleMeshCookingResult::eFAILURE && OutputStream.getSize() > 0)
		{
			const physx::PxU32 Size = OutputStream.getSize();
			OutCookedData.resize(Size);
			std::memcpy(OutCookedData.data(), OutputStream.getData(), Size);
		}
		else
		{
			FString Message = "PxCooking::cookTriangleMesh failed: ";
			Message += TriangleCookingResultToString(CookResult);
			SetError(Message.c_str());
		}

		Cooking->release();

		if (OutCookedData.empty())
		{
			return false;
		}

		SetError("");
		return true;
	}

	bool CookStaticMeshTriangleCollisionForSave(UStaticMesh* StaticMesh, const FString& ContextPath)
	{
		if (!StaticMesh || !StaticMesh->GetStaticMeshAsset())
		{
			return false;
		}

		// 렌더링 전용 StaticMesh는 BodySetup과 PhysX cooked data를 만들지 않는다.
		// StaticMesh Editor에서 triangle collision을 생성한 에셋만 아래 cooking 경로에 들어온다.
		if (!StaticMesh->IsTriangleMeshCollisionEnabled())
		{
			return true;
		}

		UBodySetup* BodySetup = StaticMesh->EnsureBodySetup();
		if (!BodySetup)
		{
			return false;
		}

		TArray<FVector> CollisionVertices;
		TArray<uint32> CollisionIndices;
		FString Error;
		if (!BuildStaticMeshTriangleCookingInput(*StaticMesh->GetStaticMeshAsset(), CollisionVertices, CollisionIndices, &Error))
		{
			BodySetup->ClearCookedTriangleMeshPhysXData();
			UE_LOG("[PhysX] StaticMesh triangle collision input failed before save. Asset=%s Reason=%s", ContextPath.c_str(), Error.c_str());
			return false;
		}

		TArray<uint8> CookedData;
		if (!CookTriangleMeshPhysX(CollisionVertices, CollisionIndices, CookedData, &Error))
		{
			BodySetup->ClearCookedTriangleMeshPhysXData();
			UE_LOG("[PhysX] StaticMesh triangle collision cooking failed before save. Asset=%s Reason=%s", ContextPath.c_str(), Error.c_str());
			return false;
		}

		BodySetup->SetCookedTriangleMeshPhysXData(
			std::move(CookedData),
			static_cast<int32>(PX_PHYSICS_VERSION),
			UBodySetup::StaticMeshTriangleCollisionCookingVersion);
		return true;
	}
}

static bool LoadStaticMeshBinary(UStaticMesh* StaticMesh, const FString& BinaryPath)
{
	FWindowsBinReader Reader(BinaryPath);
	if (!Reader.IsValid())
	{
		UE_LOG("StaticMesh binary open failed. Path=%s", BinaryPath.c_str());
		return false;
	}

	try
	{
		FAssetPackageHeader Header;
		Reader << Header;

		if (!Header.IsValid(EAssetPackageType::StaticMesh))
		{
			UE_LOG("StaticMesh binary read failed: invalid file header. Path=%s", BinaryPath.c_str());
			return false;
		}

		FAssetImportMetadata Metadata;
		Reader << Metadata;

		StaticMesh->Serialize(Reader);
	}
	catch (const std::exception&)
	{
		UE_LOG("StaticMesh binary read failed: serialization threw an exception. Path=%s", BinaryPath.c_str());
		return false;
	}

	if (!Reader.IsValid())
	{
		UE_LOG("StaticMesh binary read failed: file data is incomplete or corrupted. Path=%s", BinaryPath.c_str());
		return false;
	}

	return true;
}

static bool SaveStaticMeshBinary(UStaticMesh* StaticMesh, const FString& BinaryPath, const FString& SourcePath)
{
	// Triangle collision이 활성화된 StaticMesh package를 쓰기 직전에만 PhysX cooked binary를 갱신한다.
	// 비활성 메시에는 BodySetup 자체가 없으며, 런타임 등록 시점에는 저장된 데이터를 읽기만 한다.
	if (!CookStaticMeshTriangleCollisionForSave(StaticMesh, BinaryPath))
	{
		UE_LOG("StaticMesh binary save failed: triangle collision cooking failed. Path=%s", BinaryPath.c_str());
		return false;
	}

	FWindowsBinWriter Writer(BinaryPath);
	if (!Writer.IsValid())
	{
		UE_LOG("StaticMesh binary save failed: could not open file. Path=%s", BinaryPath.c_str());
		return false;
	}

	try
	{
		FAssetPackageHeader Header;
		Header.Type = static_cast<uint32>(EAssetPackageType::StaticMesh);
		Writer << Header;

		FAssetImportMetadata Metadata = MakeImportMetadata(SourcePath);
		Writer << Metadata;

		StaticMesh->Serialize(Writer);
	}
	catch (const std::exception&)
	{
		UE_LOG("StaticMesh binary save failed: serialization threw an exception. Path=%s", BinaryPath.c_str());
		return false;
	}

	return Writer.IsValid();
}

static bool TryGetStaticMeshSourcePathFromPackage(const FString& PackagePath, FString& OutSourcePath)
{
	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadMetadata(PackagePath, EAssetPackageType::StaticMesh, Metadata) || !Metadata.IsSourceAvailable())
	{
		return false;
	}

	if (!IsSupportedStaticMeshSourcePath(Metadata.SourcePath))
	{
		UE_LOG("StaticMesh package repair failed: unsupported source path. Package=%s Source=%s", PackagePath.c_str(), Metadata.SourcePath.c_str());
		return false;
	}

	uint64 CurrentTimestamp = 0;
	uint64 CurrentFileSize = 0;
	if (!TryGetSourceFileState(Metadata.SourcePath, CurrentTimestamp, CurrentFileSize))
	{
		UE_LOG("StaticMesh package repair failed: source file is missing. Package=%s Source=%s", PackagePath.c_str(), Metadata.SourcePath.c_str());
		return false;
	}

	OutSourcePath = Metadata.SourcePath;
	return true;
}

// 캐시 슬롯을 erase하기 전에 그 StaticMesh의 GPU 버퍼(VB/IB + LOD)를 먼저 해제한다.
// erase는 map에서 포인터만 빼므로, 먼저 풀지 않으면 RenderBuffer가 고아가 되어
// ReleaseAllGPU(캐시 잔존분만 순회)에도 안 잡히고 종료까지 LIVE_BUFFER로 샌다.
static void ReleaseCachedStaticMeshGPU(const FString& CacheKey)
{
	auto It = FMeshManager::StaticMeshCache.find(CacheKey);
	if (It == FMeshManager::StaticMeshCache.end() || !It->second) return;

	UStaticMesh* CachedMesh = It->second;
	if (FStaticMesh* Asset = CachedMesh->GetStaticMeshAsset(); Asset && Asset->RenderBuffer)
	{
		Asset->RenderBuffer->Release();
		Asset->RenderBuffer.reset();
	}
	for (uint32 LOD = 1; LOD < UStaticMesh::MAX_LOD_COUNT; ++LOD)
	{
		if (FMeshBuffer* LODBuffer = CachedMesh->GetLODMeshBuffer(LOD))
		{
			LODBuffer->Release();
		}
	}
}

static UStaticMesh* ImportStaticMeshSourceToPackage(const FString& SourcePath, const FString& PackagePath, ID3D11Device* InDevice)
{
	std::unique_ptr<FStaticMesh> NewMeshAsset = std::make_unique<FStaticMesh>();
	TArray<FStaticMaterial> ParsedMaterials;
	if (!ImportStaticMeshByExtension(SourcePath, nullptr, *NewMeshAsset, ParsedMaterials))
	{
		UE_LOG("StaticMesh import failed: empty mesh will not be added to cache. Path=%s", SourcePath.c_str());
		return nullptr;
	}

	const FString CacheKey = NormalizeProjectPath(PackagePath);
	ReleaseCachedStaticMeshGPU(CacheKey);
	FMeshManager::StaticMeshCache.erase(CacheKey);

	UStaticMesh* StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
	NewMeshAsset->PathFileName = NormalizeProjectPath(SourcePath);
	StaticMesh->SetStaticMaterials(std::move(ParsedMaterials));
	StaticMesh->SetStaticMeshAsset(NewMeshAsset.release());

	// .uasset이 없거나 현재 직렬화 형식과 맞지 않으면 원본에서 다시 만들고 캐시에 넣는다.
	if (!SaveStaticMeshBinary(StaticMesh, CacheKey, SourcePath))
	{
		return nullptr;
	}

	StaticMesh->InitResources(InDevice);
	StaticMesh->SetAssetPathFileName(CacheKey);
	FMeshManager::StaticMeshCache[CacheKey] = StaticMesh;

	FMeshManager::ScanMeshAssets();
	FMaterialManager::Get().ScanMaterialAssets();

	return StaticMesh;
}

static bool LoadSkeletalMeshBinary(USkeletalMesh* SkeletalMesh, const FString& BinaryPath)
{
	FWindowsBinReader Reader(BinaryPath);
	if (!Reader.IsValid())
	{
		UE_LOG("SkeletalMesh binary open failed. Path=%s", BinaryPath.c_str());
		return false;
	}

	try
	{
		FAssetPackageHeader Header;
		Reader << Header;

		if (!Header.IsValid(EAssetPackageType::SkeletalMesh))
		{
			UE_LOG("SkeletalMesh binary read failed: invalid file header. Path=%s", BinaryPath.c_str());
			return false;
		}

		FAssetImportMetadata Metadata;
		Reader << Metadata;

		SkeletalMesh->Serialize(Reader);
	}
	catch (const std::exception&)
	{
		UE_LOG("SkeletalMesh binary read failed: serialization threw an exception. Path=%s", BinaryPath.c_str());
		return false;
	}

	if (!Reader.IsValid())
	{
		UE_LOG("SkeletalMesh binary read failed: file data is incomplete or corrupted. Path=%s", BinaryPath.c_str());
		return false;
	}

	return true;
}

static bool RemapSkeletalMeshToSkeleton(
	FSkeletalMesh&                Mesh,
	const FReferenceSkeleton&     SourceSkeleton,
	const USkeleton*              TargetSkeleton,
	const FSkeletonBoneRemap&     Remap,
	FSkeletonCompatibilityReport* OutReport = nullptr
	)
{
	if (!TargetSkeleton)
	{
		if (OutReport)
		{
			OutReport->Result = ESkeletonCompatibilityResult::Incompatible;
			OutReport->Reason = "null target skeleton";
		}
		return false;
	}

	const FReferenceSkeleton& TargetRef = TargetSkeleton->GetReferenceSkeleton();
	TArray<FBone>             RemappedBones;
	RemappedBones.resize(TargetRef.GetNumBones());

	for (int32 TargetIndex = 0; TargetIndex < TargetRef.GetNumBones(); ++TargetIndex)
	{
		const int32 SourceIndex = TargetIndex < static_cast<int32>(Remap.TargetToSourceBone.size()) ? Remap.TargetToSourceBone[TargetIndex] : -1;

		FBone Bone;
		if (SourceIndex >= 0 && SourceIndex < static_cast<int32>(Mesh.Bones.size()))
		{
			Bone = Mesh.Bones[SourceIndex];
		}
		else
		{
			const FReferenceBone& RefBone = TargetRef.Bones[TargetIndex];
			Bone.LocalMatrix              = RefBone.LocalBindPose;
			Bone.GlobalMatrix             = RefBone.GlobalBindPose;
			Bone.InverseBindPoseMatrix    = RefBone.InverseBindPose;
			Bone.SyncSeparatedPoseDataFromLegacy();
		}

		Bone.Name                  = TargetRef.Bones[TargetIndex].Name;
		Bone.ParentIndex           = TargetRef.Bones[TargetIndex].ParentIndex;
		RemappedBones[TargetIndex] = Bone;
	}

	for (FVertexPNCTBW& Vertex : Mesh.Vertices)
	{
		for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
		{
			const int32 SourceBoneIndex = Vertex.BoneIndices[InfluenceIndex];
			const float Weight          = Vertex.BoneWeights[InfluenceIndex];

			if (Weight <= 0.0f || SourceBoneIndex < 0)
			{
				Vertex.BoneIndices[InfluenceIndex] = -1;
				Vertex.BoneWeights[InfluenceIndex] = 0.0f;
				continue;
			}

			const int32 TargetBoneIndex = Remap.GetTargetBoneIndex(SourceBoneIndex);
			if (TargetBoneIndex < 0)
			{
				if (OutReport)
				{
					OutReport->Result = ESkeletonCompatibilityResult::Incompatible;
					OutReport->Reason = "vertex references an unmapped source bone";
					if (SourceBoneIndex >= 0 && SourceBoneIndex < SourceSkeleton.GetNumBones())
					{
						OutReport->MissingBones.push_back(SourceSkeleton.Bones[SourceBoneIndex].Name);
					}
				}
				return false;
			}

			Vertex.BoneIndices[InfluenceIndex] = TargetBoneIndex;
		}
	}

	Mesh.Bones = std::move(RemappedBones);

	const FSkeletonBinding Binding      = TargetSkeleton->GetSkeletonBinding();
	Mesh.SkeletonPath                   = Binding.SkeletonPath;
	Mesh.SkeletonAssetGuid              = Binding.SkeletonAssetGuid;
	Mesh.SkeletonCompatibilitySignature = Binding.CompatibilitySignature;
	return true;
}

bool FMeshManager::ReadSkeletalMeshBinding(const FString& PackagePath, FSkeletonBinding& OutBinding)
{
	OutBinding.Reset();

	USkeletalMesh* TempMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
	const bool bLoaded = LoadSkeletalMeshBinary(TempMesh, FPaths::MakeProjectRelative(PackagePath));
	if (bLoaded)
	{
		OutBinding = TempMesh->GetSkeletonBinding();
	}
	UObjectManager::Get().DestroyObject(TempMesh);
	return bLoaded;
}

static bool SaveSkeletalMeshBinary(USkeletalMesh* SkeletalMesh, const FString& BinaryPath, const FString& SourcePath)
{
	FWindowsBinWriter Writer(BinaryPath);
	if (!Writer.IsValid())
	{
		UE_LOG("SkeletalMesh binary save failed: could not open file. Path=%s", BinaryPath.c_str());
		return false;
	}

	try
	{
		FAssetPackageHeader Header;
		Header.Type = static_cast<uint32>(EAssetPackageType::SkeletalMesh);
		Writer << Header;

		FAssetImportMetadata Metadata = MakeImportMetadata(SourcePath);
		Writer << Metadata;

		SkeletalMesh->Serialize(Writer);
	}
	catch (const std::exception&)
	{
		UE_LOG("SkeletalMesh binary save failed: serialization threw an exception. Path=%s", BinaryPath.c_str());
		return false;
	}

	return Writer.IsValid();
}

FString FMeshManager::GetStaticMeshBinaryFilePath(const FString& SourcePath)
{
	return GetMeshPackageFilePath(SourcePath, EAssetPackageType::StaticMesh);
}

FString FMeshManager::GetSkeletalMeshBinaryFilePath(const FString& SourcePath)
{
	return GetMeshPackageFilePath(SourcePath, EAssetPackageType::SkeletalMesh);
}

bool FMeshManager::IsAssetPackagePath(const FString& Path)
{
	return FAssetPackage::IsAssetPackagePath(Path);
}

bool FMeshManager::ReimportStaticMesh(const FString& BinaryPath, ID3D11Device* Device, UStaticMesh*& OutStaticMesh)
{
	OutStaticMesh = nullptr;

	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadMetadata(BinaryPath, EAssetPackageType::StaticMesh, Metadata)) return false;

	uint64 CurrentTimestamp = 0;
	uint64 CurrentFileSize = 0;
	if (!TryGetSourceFileState(Metadata.SourcePath, CurrentTimestamp, CurrentFileSize))
	{
		UE_LOG("StaticMesh reimport failed: source file is missing. Package=%s, Source=%s", BinaryPath.c_str(), Metadata.SourcePath.c_str());
		return false;
	}

	std::unique_ptr<FStaticMesh> NewMeshAsset = std::make_unique<FStaticMesh>();
	TArray<FStaticMaterial> ParsedMaterials;
	if (!ImportStaticMeshByExtension(Metadata.SourcePath, nullptr, *NewMeshAsset, ParsedMaterials))
	{
		return false;
	}

	const FString PackagePath = NormalizeProjectPath(BinaryPath);
	bool bTriangleMeshCollisionEnabled = false;
	auto ExistingMeshIt = StaticMeshCache.find(PackagePath);
	if (ExistingMeshIt != StaticMeshCache.end() && ExistingMeshIt->second)
	{
		bTriangleMeshCollisionEnabled = ExistingMeshIt->second->IsTriangleMeshCollisionEnabled();
	}
	else
	{
		// reimport는 새 UStaticMesh를 만들기 때문에 기존 에셋의 editor opt-in 상태를 먼저 읽어 둔다.
		// 활성화된 에셋이면 아래 SaveStaticMeshBinary()가 변경된 vertex/index로 cooked binary를 다시 만든다.
		UStaticMesh* ExistingMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
		if (LoadStaticMeshBinary(ExistingMesh, PackagePath))
		{
			bTriangleMeshCollisionEnabled = ExistingMesh->IsTriangleMeshCollisionEnabled();
		}
		UObjectManager::Get().DestroyObject(ExistingMesh);
	}
	ReleaseCachedStaticMeshGPU(PackagePath);
	StaticMeshCache.erase(PackagePath);

	UStaticMesh* StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
	NewMeshAsset->PathFileName = Metadata.SourcePath;
	StaticMesh->SetStaticMaterials(std::move(ParsedMaterials));
	StaticMesh->SetStaticMeshAsset(NewMeshAsset.release());
	StaticMesh->SetTriangleMeshCollisionEnabled(bTriangleMeshCollisionEnabled);

	if (!SaveStaticMeshBinary(StaticMesh, PackagePath, Metadata.SourcePath)) return false;

	StaticMesh->InitResources(Device);
	StaticMesh->SetAssetPathFileName(PackagePath);
	StaticMeshCache[PackagePath] = StaticMesh;
	OutStaticMesh = StaticMesh;

	ScanMeshAssets();
	FMaterialManager::Get().ScanMaterialAssets();

	return true;
}

bool FMeshManager::SaveStaticMesh(UStaticMesh* StaticMesh)
{
	if (!StaticMesh)
	{
		return false;
	}

	const FStaticMesh* MeshAsset = StaticMesh->GetStaticMeshAsset();
	const FString& PackagePath = StaticMesh->GetAssetPathFileName();
	if (!MeshAsset || PackagePath.empty() || PackagePath == "None")
	{
		return false;
	}

	// StaticMesh Editor에서 triangle collision 생성/제거를 확정할 때 사용한다.
	// 생성 시에는 SaveStaticMeshBinary() 내부에서 cook하고, 제거 시에는 BodySetup 없는 package를 기록한다.
	return SaveStaticMeshBinary(StaticMesh, PackagePath, MeshAsset->PathFileName);
}

bool FMeshManager::SaveSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
	if (!SkeletalMesh)
	{
		return false;
	}

	const FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();
	const FString& PackagePath = SkeletalMesh->GetAssetPathFileName();
	if (!MeshAsset || PackagePath.empty() || PackagePath == "None")
	{
		return false;
	}

	// PhysicsAsset Editor에서 새 asset을 연결한 경우 SkeletalMesh의 soft reference도 package에 기록한다.
	// PhysicsAsset 본문과 SkeletalMesh 참조는 별도 파일이므로 둘 다 저장해야 다음 로드에서도 연결이 유지된다.
	return SaveSkeletalMeshBinary(SkeletalMesh, PackagePath, MeshAsset->PathFileName);
}

bool FMeshManager::ReimportSkeletalMesh(const FString& BinaryPath, ID3D11Device* Device, USkeletalMesh*& OutSkeletalMesh)
{
	OutSkeletalMesh = nullptr;

	FAssetImportMetadata Metadata;
	if (!FAssetPackage::ReadMetadata(BinaryPath, EAssetPackageType::SkeletalMesh, Metadata)) return false;

	uint64 CurrentTimestamp = 0;
	uint64 CurrentFileSize = 0;
	if (!TryGetSourceFileState(Metadata.SourcePath, CurrentTimestamp, CurrentFileSize))
	{
		UE_LOG("SkeletalMesh reimport failed: source file is missing. Package=%s, Source=%s", BinaryPath.c_str(), Metadata.SourcePath.c_str());
		return false;
	}

	const FString    PackagePath = NormalizeProjectPath(BinaryPath);
	FSkeletonBinding ExistingBinding;
	ReadSkeletalMeshBinding(PackagePath, ExistingBinding);
	FString ExistingPhysicsAssetPath = "None";
	auto ExistingMeshIt = SkeletalMeshCache.find(PackagePath);
	if (ExistingMeshIt != SkeletalMeshCache.end() && ExistingMeshIt->second)
	{
		ExistingPhysicsAssetPath = ExistingMeshIt->second->GetPhysicsAssetPath();
	}
	else
	{
		USkeletalMesh* ExistingMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
		if (LoadSkeletalMeshBinary(ExistingMesh, PackagePath))
		{
			ExistingPhysicsAssetPath = ExistingMesh->GetPhysicsAssetPath();
		}
		UObjectManager::Get().DestroyObject(ExistingMesh);
	}

	const FString DefaultSkeletonPath  = FSkeletonManager::GetSkeletonPackagePath(Metadata.SourcePath);
	const bool    bUseExistingSkeleton = ExistingBinding.HasSkeletonPath() && ExistingBinding.SkeletonPath != DefaultSkeletonPath;

	bool bImported = false;
	if (bUseExistingSkeleton)
	{
		FSkeletalMeshImportRequest Request;
		Request.SourceFbxPath            = Metadata.SourcePath;
		Request.TargetSkeletonPath       = ExistingBinding.SkeletonPath;
		Request.DestinationPackagePath   = PackagePath;
		Request.bOverwriteExistingAssets = true;
		bImported = ImportSkeletalMesh(Request, Device, OutSkeletalMesh);
	}
	else
	{
		bImported = ImportSkeletalMeshAsNew(Metadata.SourcePath, Device, OutSkeletalMesh);
	}

	if (!bImported || !OutSkeletalMesh || ExistingPhysicsAssetPath.empty() || ExistingPhysicsAssetPath == "None")
	{
		return bImported;
	}

	// FBX reimport는 새 USkeletalMesh를 만들지만, 사용자가 PhysicsAsset Editor에서 만든 body는 별도 asset이다.
	// 기존 PhysicsAsset을 다시 연결하고 메시 package도 저장하여 명시적으로 만든 physics 설정을 보존한다.
	if (UPhysicsAsset* ExistingPhysicsAsset = FPhysicsAssetManager::Get().Load(ExistingPhysicsAssetPath))
	{
		OutSkeletalMesh->SetPhysicsAsset(ExistingPhysicsAsset);
		return SaveSkeletalMesh(OutSkeletalMesh);
	}

	return true;
}

bool FMeshManager::IsStaticMeshPackage(const FString& Path)
{
	FAssetImportMetadata Metadata;
	return FAssetPackage::ReadMetadata(Path, EAssetPackageType::StaticMesh, Metadata);
}

bool FMeshManager::IsSkeletalMeshPackage(const FString& Path)
{
	FAssetImportMetadata Metadata;
	return FAssetPackage::ReadMetadata(Path, EAssetPackageType::SkeletalMesh, Metadata);
}

void FMeshManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (auto& [Path, Mesh] : StaticMeshCache)
	{
		Collector.AddReferencedObject(Mesh);
	}

	for (auto& [Path, Mesh] : SkeletalMeshCache)
	{
		Collector.AddReferencedObject(Mesh);
	}
}

void FMeshManager::ScanMeshAssets()
{
	AvailableStaticMeshFiles.clear();
	AvailableSkeletalMeshFiles.clear();

	const std::filesystem::path MeshCacheRoot = FPaths::RootDir() + L"Content\\";
	if (!std::filesystem::exists(MeshCacheRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(MeshCacheRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		std::wstring Ext = Path.extension().wstring();
		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);

		if (Ext != MeshBinary::StaticMeshBinaryExtension) continue;	

		// Mesh package 목록은 .uasset만 대상으로 하고, 파일 header의 타입으로 Static/Skeletal을 나눈다.
		TArray<FAssetListItem>* TargetList = nullptr;

		FString RelPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());

		FAssetImportMetadata Metadata;
		if (FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::StaticMesh, Metadata))
		{
			TargetList = &AvailableStaticMeshFiles;
		}
		else if (FAssetPackage::ReadMetadata(RelPath, EAssetPackageType::SkeletalMesh, Metadata))
		{
			TargetList = &AvailableSkeletalMeshFiles;
		}
		else
		{
			continue;
		}

		FAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.stem().wstring());
		Item.FullPath = RelPath;
		TargetList->push_back(std::move(Item));
	}
}

void FMeshManager::ScanMeshSourceFiles()
{
	AvailableStaticMeshSourceFiles.clear();

	const std::filesystem::path DataRoot = FPaths::RootDir() + L"Content/Data/";

	if (!std::filesystem::exists(DataRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(DataRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		std::wstring Ext = Path.extension().wstring();

		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		if (Ext != L".obj" && Ext != L".fbx") continue;

		FAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.filename().wstring());
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
		AvailableStaticMeshSourceFiles.push_back(std::move(Item));
	}
}

UStaticMesh* FMeshManager::LoadStaticMesh(const FString& PathFileName, const FImportOptions& Options, ID3D11Device* InDevice)
{
	const bool bInputIsPackage = IsAssetPackagePath(PathFileName);
	if (bInputIsPackage)
	{
		UE_LOG("StaticMesh load failed: StaticMesh binary cannot be loaded as StaticMesh. Path=%s", PathFileName.c_str());
		return nullptr;
	}
	if (!IsSupportedStaticMeshSourcePath(PathFileName))
	{
		UE_LOG("StaticMesh import failed: option import only supports source .obj/.fbx paths. Path=%s", PathFileName.c_str());
		return nullptr;
	}

	const FString CacheKey = GetStaticMeshBinaryFilePath(PathFileName);

	// import 옵션이 바뀌면 같은 원본도 다른 Mesh가 될 수 있다.
	// 그래서 기존 캐시를 지우고 새 .uasset package를 만든다.
	ReleaseCachedStaticMeshGPU(CacheKey);
	StaticMeshCache.erase(CacheKey);

	std::unique_ptr<FStaticMesh> NewMeshAsset = std::make_unique<FStaticMesh>();
	TArray<FStaticMaterial> ParsedMaterials;
	if (!ImportStaticMeshByExtension(PathFileName, &Options, *NewMeshAsset, ParsedMaterials))
	{
		UE_LOG("StaticMesh import failed: empty mesh will not be added to cache. Path=%s", PathFileName.c_str());
		return nullptr;
	}

	UStaticMesh* StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
	NewMeshAsset->PathFileName = NormalizeProjectPath(PathFileName);
	StaticMesh->SetStaticMaterials(std::move(ParsedMaterials));
	StaticMesh->SetStaticMeshAsset(NewMeshAsset.release());

	// import가 끝난 StaticMesh는 .uasset package로 저장한다.
	// 다음 로드부터는 무거운 원본 파싱을 건너뛸 수 있다.
	if (!SaveStaticMeshBinary(StaticMesh, CacheKey, PathFileName))
	{
		return nullptr;
	}

	StaticMesh->InitResources(InDevice);
	StaticMesh->SetAssetPathFileName(CacheKey);
	StaticMeshCache[CacheKey] = StaticMesh;

	ScanMeshAssets();
	FMaterialManager::Get().ScanMaterialAssets();

	return StaticMesh;
}

UStaticMesh* FMeshManager::LoadStaticMesh(const FString& PathFileName, ID3D11Device* InDevice)
{
	const bool bInputIsPackage = IsAssetPackagePath(PathFileName);
	if (!bInputIsPackage && !IsSupportedStaticMeshSourcePath(PathFileName))
	{
		UE_LOG("StaticMesh load failed: unsupported path. Path=%s", PathFileName.c_str());
		return nullptr;
	}

	const FString CacheKey = GetStaticMeshBinaryFilePath(PathFileName);
	const std::filesystem::path BinaryPath(FPaths::ToWide(CacheKey));

	auto It = StaticMeshCache.find(CacheKey);
	if (It != StaticMeshCache.end())
	{
		bool bMissingSource = false;
		if (std::filesystem::exists(BinaryPath) && IsPackageSourceStale(CacheKey, EAssetPackageType::StaticMesh, bMissingSource) && !bMissingSource)
		{
			UStaticMesh* ReimportedStaticMesh = nullptr;
			if (ReimportStaticMesh(CacheKey, InDevice, ReimportedStaticMesh) && ReimportedStaticMesh)
			{
				UE_LOG("StaticMesh package was stale and has been reimported. Binary=%s", CacheKey.c_str());
				return ReimportedStaticMesh;
			}
			UE_LOG("StaticMesh package is stale but reimport failed; using cached package. Binary=%s", CacheKey.c_str());
		}
		return It->second;
	}

	if (std::filesystem::exists(BinaryPath))
	{
		UStaticMesh* StaticMesh = UObjectManager::Get().CreateObject<UStaticMesh>();
		if (LoadStaticMeshBinary(StaticMesh, CacheKey))
		{
			bool bMissingSource = false;
			if (IsPackageSourceStale(CacheKey, EAssetPackageType::StaticMesh, bMissingSource))
			{
				UE_LOG("StaticMesh package is stale. Package=%s MissingSource=%s", CacheKey.c_str(), bMissingSource ? "true" : "false");
				if (!bMissingSource)
				{
					UStaticMesh* ReimportedStaticMesh = nullptr;
					if (ReimportStaticMesh(CacheKey, InDevice, ReimportedStaticMesh) && ReimportedStaticMesh)
					{
						UE_LOG("StaticMesh package was reimported from changed source. Binary=%s", CacheKey.c_str());
						return ReimportedStaticMesh;
					}
					UE_LOG("StaticMesh package reimport failed; using existing binary. Binary=%s", CacheKey.c_str());
				}
			}

			StaticMesh->InitResources(InDevice);
			StaticMesh->SetAssetPathFileName(CacheKey);
			StaticMeshCache[CacheKey] = StaticMesh;
			return StaticMesh;
		}

		if (bInputIsPackage)
		{
			FString SourcePath;
			if (TryGetStaticMeshSourcePathFromPackage(CacheKey, SourcePath))
			{
				UE_LOG("StaticMesh binary load failed: reimporting from package metadata. Source=%s Binary=%s", SourcePath.c_str(), CacheKey.c_str());
				return ImportStaticMeshSourceToPackage(SourcePath, CacheKey, InDevice);
			}
			return nullptr;
		}

		UE_LOG("StaticMesh binary load failed: source path is available, reimporting. Source=%s Binary=%s", PathFileName.c_str(), CacheKey.c_str());
	}
	else if (bInputIsPackage)
	{
		UE_LOG("StaticMesh load failed: StaticMesh binary file does not exist. Path=%s", CacheKey.c_str());
		return nullptr;
	}

	return ImportStaticMeshSourceToPackage(PathFileName, CacheKey, InDevice);
}

void FMeshManager::ScanFbxSourceFiles()
{
	AvailableFbxSourceFiles.clear();

	const std::filesystem::path DataRoot = FPaths::RootDir() + L"Content/Data/";

	if (!std::filesystem::exists(DataRoot))
	{
		return;
	}

	const std::filesystem::path ProjectRoot(FPaths::RootDir());

	for (const auto& Entry : std::filesystem::recursive_directory_iterator(DataRoot))
	{
		if (!Entry.is_regular_file()) continue;

		const std::filesystem::path& Path = Entry.path();
		std::wstring Ext = Path.extension().wstring();

		std::transform(Ext.begin(), Ext.end(), Ext.begin(), ::towlower);
		if (Ext != L".fbx") continue;

		FAssetListItem Item;
		Item.DisplayName = FPaths::ToUtf8(Path.filename().wstring());
		Item.FullPath = FPaths::ToUtf8(Path.lexically_relative(ProjectRoot).generic_wstring());
		AvailableFbxSourceFiles.push_back(std::move(Item));
	}
}

void FMeshManager::ReleaseAllGPU()
{
	// Static Mesh
	for (auto& [Key, Mesh] : StaticMeshCache)
	{
		if (Mesh)
		{
			FStaticMesh* Asset = Mesh->GetStaticMeshAsset();
			if (Asset && Asset->RenderBuffer)
			{
				Asset->RenderBuffer->Release();
				Asset->RenderBuffer.reset();
			}
			// LOD 버퍼도 해제
			for (uint32 LOD = 1; LOD < UStaticMesh::MAX_LOD_COUNT; ++LOD)
			{
				FMeshBuffer* LODBuffer = Mesh->GetLODMeshBuffer(LOD);
				if (LODBuffer)
				{
					LODBuffer->Release();
				}
			}
		}
	}
	StaticMeshCache.clear();

	// Skeletal Mesh
	for (auto& [Key, Mesh] : SkeletalMeshCache)
	{
		if (Mesh)
		{
			FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
			if (Asset && Asset->RenderBuffer)
			{
				Asset->RenderBuffer->Release();
				Asset->RenderBuffer.reset();
			}
		}
	}
	SkeletalMeshCache.clear();
}

static bool SaveImportedSkeletonAsset(
	const FString&       SourceFbxPath,
	FReferenceSkeleton&& ImportedReferenceSkeleton,
	USkeleton*&          OutSkeleton
	)
{
	OutSkeleton = nullptr;

	if (ImportedReferenceSkeleton.GetNumBones() <= 0)
	{
		UE_LOG("Skeleton import failed: imported reference skeleton has no bones. Source=%s", SourceFbxPath.c_str());
		return false;
	}

	const FString SkeletonPath           = FSkeletonManager::GetSkeletonPackagePath(SourceFbxPath);
	const FString CompatibilitySignature = FSkeletonManager::BuildCompatibilitySignature(ImportedReferenceSkeleton);

	USkeleton* ExistingSkeleton = nullptr;
	if (std::filesystem::exists(ResolveProjectPath(SkeletonPath)))
	{
		ExistingSkeleton = FSkeletonManager::Get().LoadSkeleton(SkeletonPath);
	}

	USkeleton* Skeleton = UObjectManager::Get().CreateObject<USkeleton>();
	Skeleton->SetAssetPathFileName(SkeletonPath);
	Skeleton->GetMutableReferenceSkeleton() = std::move(ImportedReferenceSkeleton);
	Skeleton->SetCompatibilitySignature(CompatibilitySignature);

	if (ExistingSkeleton)
	{
		FSkeletonCompatibilityReport ExistingReport;
		const bool                   bSameStructure = FSkeletonManager::AreSkeletonsSameStructure(
			ExistingSkeleton->GetReferenceSkeleton(),
			Skeleton->GetReferenceSkeleton(),
			&ExistingReport
		);

		if (bSameStructure)
		{
			Skeleton->SetSkeletonAssetGuid(ExistingSkeleton->GetSkeletonAssetGuid());
		}
		else
		{
			UE_LOG(
				"Skeleton import: skeleton structure changed, issuing new SkeletonAssetGuid. Path=%s Reason=%s",
				SkeletonPath.c_str(),
				ExistingReport.Reason.c_str()
			);
			Skeleton->SetSkeletonAssetGuid(
				FSkeletonManager::MakeSkeletonAssetGuid(SkeletonPath + "#changed", CompatibilitySignature)
			);
		}
	}
	else
	{
		Skeleton->SetSkeletonAssetGuid(FSkeletonManager::MakeSkeletonAssetGuid(SkeletonPath, CompatibilitySignature));
	}

	Skeleton->RebuildBoneNameCache();

	if (!FSkeletonManager::Get().SaveSkeleton(Skeleton, SkeletonPath, SourceFbxPath))
	{
		UE_LOG("Skeleton import failed: skeleton save failed. Path=%s", SourceFbxPath.c_str());
		return false;
	}

	FSkeletonManager::Get().ScanSkeletonAssets();
	OutSkeleton = Skeleton;
	return true;
}

USkeletalMesh* FMeshManager::LoadSkeletalMesh(const FString& PathFileName, ID3D11Device* InDevice)
{
	const std::wstring Ext = GetLowerExtension(PathFileName);

	const bool bInputIsPackage = IsAssetPackagePath(PathFileName);
	if (!bInputIsPackage && !IsSupportedSkeletalMeshSourcePath(PathFileName))
	{
		UE_LOG("SkeletalMesh load failed: unsupported path. Path=%s", PathFileName.c_str());
		return nullptr;
	}

	const FString CacheKey = GetSkeletalMeshBinaryFilePath(PathFileName);
	auto It = SkeletalMeshCache.find(CacheKey);
	if (It != SkeletalMeshCache.end())
	{
		return It->second;
	}

	const std::filesystem::path BinaryPath(FPaths::ToWide(CacheKey));
	if (std::filesystem::exists(BinaryPath))
	{
		USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
		if (LoadSkeletalMeshBinary(SkeletalMesh, CacheKey))
		{
			bool bMissingSource = false;
			if (IsPackageSourceStale(CacheKey, EAssetPackageType::SkeletalMesh, bMissingSource))
			{
				UE_LOG("SkeletalMesh package is stale. Package=%s MissingSource=%s", CacheKey.c_str(), bMissingSource ? "true" : "false");
			}

			const FSkeletonBinding& Binding = SkeletalMesh->GetSkeletonBinding();
			if (!Binding.SkeletonPath.empty() && Binding.SkeletonPath != "None")
			{
				USkeleton* Skeleton = FSkeletonManager::Get().LoadSkeleton(Binding.SkeletonPath);
				SkeletalMesh->SetSkeleton(Skeleton);
			}

			SkeletalMesh->InitResources(InDevice);
			SkeletalMesh->SetAssetPathFileName(CacheKey);
			SkeletalMeshCache[CacheKey] = SkeletalMesh;
			return SkeletalMesh;
		}

		if (bInputIsPackage)
		{
			USkeletalMesh* ReimportedSkeletalMesh = nullptr;
			if (ReimportSkeletalMesh(CacheKey, InDevice, ReimportedSkeletalMesh) && ReimportedSkeletalMesh)
			{
				UE_LOG("SkeletalMesh binary load failed: reimported from package metadata. Binary=%s", CacheKey.c_str());
				return ReimportedSkeletalMesh;
			}
			return nullptr;
		}

		UE_LOG("SkeletalMesh binary load failed: source path is available, reimporting. Source=%s Binary=%s", PathFileName.c_str(), CacheKey.c_str());
	}
	else if (bInputIsPackage)
	{
		UE_LOG("SkeletalMesh load failed: SkeletalMesh binary file does not exist. Path=%s", CacheKey.c_str());
		return nullptr;
	}

	USkeletalMesh* ImportedSkeletalMesh = nullptr;
	if (!ImportSkeletalMeshAsNew(PathFileName, InDevice, ImportedSkeletalMesh))
	{
		UE_LOG("SkeletalMesh import failed: empty mesh will not be added to cache. Path=%s", PathFileName.c_str());
		return nullptr;
	}

	return ImportedSkeletalMesh;
}

bool FMeshManager::ImportSkeletalMeshAsNew(const FString& SourceFbxPath, ID3D11Device* Device, USkeletalMesh*& OutSkeletalMesh)
{
	OutSkeletalMesh = nullptr;

	if (!IsSupportedSkeletalMeshSourcePath(SourceFbxPath))
	{
		UE_LOG("SkeletalMesh import failed: only source FBX paths can be imported. Path=%s", SourceFbxPath.c_str());
		return false;
	}

	FFbxSkeletalMeshImportResult ImportResult;
	if (!FFbxImporter::ImportSkeletalMesh(SourceFbxPath, ImportResult))
	{
		return false;
	}

	USkeleton* Skeleton = nullptr;
	if (!SaveImportedSkeletonAsset(SourceFbxPath, std::move(ImportResult.Skeleton), Skeleton))
	{
		UE_LOG("SkeletalMesh import failed: skeleton save failed. Path=%s", SourceFbxPath.c_str());
		return false;
	}

	const FString          SkeletonPath    = Skeleton->GetAssetPathFileName();
	const FSkeletonBinding SkeletonBinding = Skeleton->GetSkeletonBinding();

	for (UAnimSequence* Sequence : ImportResult.AnimSequences)
	{
		if (!Sequence)
		{
			continue;
		}

		Sequence->SetSkeletonBinding(SkeletonBinding);

		const FString AnimPath = FAnimationManager::GetAnimationPathForSkeleton(SourceFbxPath, Sequence->GetName(), SkeletonPath);
		if (!FAnimationManager::Get().SaveAnimation(Sequence, AnimPath, SourceFbxPath))
		{
			UE_LOG("SkeletalMesh import failed: animation save failed. Source=%s Anim=%s", SourceFbxPath.c_str(), Sequence->GetName().c_str());
		}
	}

	std::unique_ptr<FSkeletalMesh> NewMesh  = std::make_unique<FSkeletalMesh>(std::move(ImportResult.Mesh));
	NewMesh->PathFileName                   = NormalizeProjectPath(SourceFbxPath);
	NewMesh->SkeletonPath                   = SkeletonBinding.SkeletonPath;
	NewMesh->SkeletonAssetGuid              = SkeletonBinding.SkeletonAssetGuid;
	NewMesh->SkeletonCompatibilitySignature = SkeletonBinding.CompatibilitySignature;

	const FString PackagePath = GetSkeletalMeshBinaryFilePath(SourceFbxPath);
	SkeletalMeshCache.erase(PackagePath);

	USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
	SkeletalMesh->SetSkeletalMaterials(std::move(ImportResult.Materials));
	SkeletalMesh->SetSkeletalMeshAsset(NewMesh.release());
	SkeletalMesh->SetSkeleton(Skeleton);
	if (!SaveSkeletalMeshBinary(SkeletalMesh, PackagePath, SourceFbxPath))
	{
		return false;
	}

	SkeletalMesh->InitResources(Device);
	SkeletalMesh->SetAssetPathFileName(PackagePath);
	SkeletalMeshCache[PackagePath] = SkeletalMesh;
	OutSkeletalMesh                = SkeletalMesh;

	ScanMeshAssets();
	FMaterialManager::Get().ScanMaterialAssets();
	return true;
}

bool FMeshManager::ImportSkeletonAsNew(const FString& SourceFbxPath, USkeleton*& OutSkeleton)
{
	OutSkeleton = nullptr;

	if (!IsSupportedSkeletalMeshSourcePath(SourceFbxPath))
	{
		UE_LOG("Skeleton import failed: only source FBX paths can be imported. Path=%s", SourceFbxPath.c_str());
		return false;
	}

	FFbxSkeletonImportResult ImportResult;
	if (!FFbxImporter::ImportSkeletonOnly(SourceFbxPath, ImportResult))
	{
		return false;
	}

	return SaveImportedSkeletonAsset(SourceFbxPath, std::move(ImportResult.SourceSkeleton), OutSkeleton);
}

bool FMeshManager::ImportSkeletalMesh(const FSkeletalMeshImportRequest& Request, ID3D11Device* Device, USkeletalMesh*& OutSkeletalMesh)
{
	OutSkeletalMesh = nullptr;

	if (!IsSupportedSkeletalMeshSourcePath(Request.SourceFbxPath))
	{
		UE_LOG("SkeletalMesh import failed: only source FBX paths can be imported. Path=%s", Request.SourceFbxPath.c_str());
		return false;
	}

	if (Request.TargetSkeletonPath.empty() || Request.TargetSkeletonPath == "None")
	{
		UE_LOG("SkeletalMesh import failed: target skeleton is required. Source=%s", Request.SourceFbxPath.c_str());
		return false;
	}

	USkeleton* TargetSkeleton = FSkeletonManager::Get().LoadSkeleton(Request.TargetSkeletonPath);
	if (!TargetSkeleton)
	{
		UE_LOG("SkeletalMesh import failed: target skeleton not found. Path=%s", Request.TargetSkeletonPath.c_str());
		return false;
	}

	FFbxSkeletalMeshOnlyImportResult ImportResult;
	if (!FFbxImporter::ImportSkeletalMeshOnly(Request.SourceFbxPath, ImportResult))
	{
		return false;
	}

	FSkeletonBoneRemap           Remap;
	FSkeletonCompatibilityReport Report;
	if (!FSkeletonManager::BuildBoneRemapByName(
		ImportResult.SourceSkeleton,
		TargetSkeleton->GetReferenceSkeleton(),
		Remap,
		&Report,
		!Request.bAllowTargetExtraBones
	))
	{
		UE_LOG(
			"SkeletalMesh import failed: skeleton remap failed. Source=%s Target=%s Reason=%s",
			Request.SourceFbxPath.c_str(),
			Request.TargetSkeletonPath.c_str(),
			Report.Reason.c_str()
		);
		return false;
	}

	if (!RemapSkeletalMeshToSkeleton(ImportResult.Mesh, ImportResult.SourceSkeleton, TargetSkeleton, Remap, &Report))
	{
		UE_LOG(
			"SkeletalMesh import failed: mesh remap failed. Source=%s Target=%s Reason=%s",
			Request.SourceFbxPath.c_str(),
			Request.TargetSkeletonPath.c_str(),
			Report.Reason.c_str()
		);
		return false;
	}

	const FString PackagePath = Request.DestinationPackagePath.empty() ? GetSkeletalMeshBinaryFilePath(Request.SourceFbxPath)
	: NormalizeProjectPath(Request.DestinationPackagePath);

	if (!Request.bOverwriteExistingAssets && std::filesystem::exists(ResolveProjectPath(PackagePath)))
	{
		UE_LOG("SkeletalMesh import skipped: destination exists. Path=%s", PackagePath.c_str());
		return false;
	}

	ImportResult.Mesh.PathFileName = NormalizeProjectPath(Request.SourceFbxPath);

	SkeletalMeshCache.erase(PackagePath);
	USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
	SkeletalMesh->SetSkeletalMaterials(std::move(ImportResult.Materials));
	SkeletalMesh->SetSkeletalMeshAsset(new FSkeletalMesh(std::move(ImportResult.Mesh)));
	SkeletalMesh->SetSkeleton(TargetSkeleton);
	if (!SaveSkeletalMeshBinary(SkeletalMesh, PackagePath, Request.SourceFbxPath))
	{
		return false;
	}

	SkeletalMesh->InitResources(Device);
	SkeletalMesh->SetAssetPathFileName(PackagePath);
	SkeletalMeshCache[PackagePath] = SkeletalMesh;
	OutSkeletalMesh                = SkeletalMesh;

	ScanMeshAssets();
	FMaterialManager::Get().ScanMaterialAssets();
	return true;
}

bool FMeshManager::ImportFbxScene(
	const FFbxSceneImportRequest& Request,
	ID3D11Device*                 Device,
	FFbxSceneImportResult&        OutResult
	)
{
	OutResult = FFbxSceneImportResult();

	if (!IsSupportedSkeletalMeshSourcePath(Request.SourceFbxPath))
	{
		UE_LOG("FBX import failed: only source FBX paths can be imported. Path=%s", Request.SourceFbxPath.c_str());
		return false;
	}

	if (!Request.bImportSkeleton && !Request.bImportSkin && !Request.bImportAnimations)
	{
		UE_LOG("FBX import failed: no import part was selected. Source=%s", Request.SourceFbxPath.c_str());
		return false;
	}

	FFbxSkeletalSceneImportOptions ImportOptions;
	ImportOptions.bImportSkeleton                       = Request.bImportSkeleton;
	ImportOptions.bImportSkin                           = Request.bImportSkin;
	ImportOptions.bImportAnimations                     = Request.bImportAnimations;
	ImportOptions.AnimationOptions.SelectedStackIndices = Request.SelectedAnimationStackIndices;

	FFbxSkeletalSceneImportResult ImportResult;
	FString                       ImportMessage;
	if (!FFbxImporter::ImportSkeletalScene(Request.SourceFbxPath, ImportOptions, ImportResult, &ImportMessage))
	{
		UE_LOG(
			"FBX import failed: skeletal scene import failed. Source=%s Reason=%s",
			Request.SourceFbxPath.c_str(),
			ImportMessage.c_str()
		);
		return false;
	}

	FReferenceSkeleton SourceSkeleton = ImportResult.SourceSkeleton;
	if (SourceSkeleton.GetNumBones() <= 0)
	{
		UE_LOG("FBX import failed: source skeleton is empty. Source=%s", Request.SourceFbxPath.c_str());
		return false;
	}

	FString    EffectiveSkeletonPath = Request.TargetSkeletonPath;
	USkeleton* EffectiveSkeleton     = nullptr;

	if (Request.bImportSkeleton)
	{
		FReferenceSkeleton SkeletonToSave   = SourceSkeleton;
		USkeleton*         ImportedSkeleton = nullptr;
		if (!SaveImportedSkeletonAsset(Request.SourceFbxPath, std::move(SkeletonToSave), ImportedSkeleton))
		{
			UE_LOG("FBX import failed: skeleton save failed. Source=%s", Request.SourceFbxPath.c_str());
			return false;
		}

		OutResult.Skeleton = ImportedSkeleton;
		EffectiveSkeleton  = ImportedSkeleton;
		if (ImportedSkeleton)
		{
			EffectiveSkeletonPath = ImportedSkeleton->GetAssetPathFileName();
		}
	}

	const bool bNeedsTargetSkeleton = Request.bImportSkin || Request.bImportAnimations;
	if (bNeedsTargetSkeleton && (EffectiveSkeletonPath.empty() || EffectiveSkeletonPath == "None"))
	{
		UE_LOG(
			"FBX import failed: skin or animation import requires an imported or target skeleton. Source=%s",
			Request.SourceFbxPath.c_str()
		);
		return false;
	}

	if (bNeedsTargetSkeleton && !EffectiveSkeleton)
	{
		EffectiveSkeleton = FSkeletonManager::Get().LoadSkeleton(EffectiveSkeletonPath);
	}

	if (bNeedsTargetSkeleton && !EffectiveSkeleton)
	{
		UE_LOG("FBX import failed: target skeleton not found. Path=%s", EffectiveSkeletonPath.c_str());
		return false;
	}

	if (Request.bImportSkin)
	{
		FSkeletonBoneRemap           Remap;
		FSkeletonCompatibilityReport Report;
		if (!FSkeletonManager::BuildBoneRemapByName(
			SourceSkeleton,
			EffectiveSkeleton->GetReferenceSkeleton(),
			Remap,
			&Report,
			!Request.bAllowTargetExtraBones
		))
		{
			UE_LOG(
				"FBX import failed: skeletal mesh remap failed. Source=%s Target=%s Reason=%s",
				Request.SourceFbxPath.c_str(),
				EffectiveSkeletonPath.c_str(),
				Report.Reason.c_str()
			);
			return false;
		}

		if (!RemapSkeletalMeshToSkeleton(ImportResult.Mesh, SourceSkeleton, EffectiveSkeleton, Remap, &Report))
		{
			UE_LOG(
				"FBX import failed: skeletal mesh remap failed. Source=%s Target=%s Reason=%s",
				Request.SourceFbxPath.c_str(),
				EffectiveSkeletonPath.c_str(),
				Report.Reason.c_str()
			);
			return false;
		}

		const FString PackagePath = Request.DestinationPackagePath.empty()
		? GetSkeletalMeshBinaryFilePath(Request.SourceFbxPath) : NormalizeProjectPath(Request.DestinationPackagePath);

		if (!Request.bOverwriteExistingAssets && std::filesystem::exists(ResolveProjectPath(PackagePath)))
		{
			UE_LOG("FBX skeletal mesh import skipped: destination exists. Path=%s", PackagePath.c_str());
		}
		else
		{
			ImportResult.Mesh.PathFileName = NormalizeProjectPath(Request.SourceFbxPath);

			SkeletalMeshCache.erase(PackagePath);
			USkeletalMesh* SkeletalMesh = UObjectManager::Get().CreateObject<USkeletalMesh>();
			SkeletalMesh->SetSkeletalMaterials(std::move(ImportResult.Materials));
			SkeletalMesh->SetSkeletalMeshAsset(new FSkeletalMesh(std::move(ImportResult.Mesh)));
			SkeletalMesh->SetSkeleton(EffectiveSkeleton);
			if (!SaveSkeletalMeshBinary(SkeletalMesh, PackagePath, Request.SourceFbxPath))
			{
				return false;
			}

			SkeletalMesh->InitResources(Device);
			SkeletalMesh->SetAssetPathFileName(PackagePath);
			SkeletalMeshCache[PackagePath] = SkeletalMesh;
			OutResult.SkeletalMesh         = SkeletalMesh;
		}
	}

	if (Request.bImportAnimations)
	{
		if (!FAnimationManager::Get().SaveImportedAnimationsForSkeleton(
			Request.SourceFbxPath,
			SourceSkeleton,
			EffectiveSkeletonPath,
			Request.DestinationAnimationDirectory,
			Request.bAllowTargetExtraBones,
			Request.bOverwriteExistingAssets,
			ImportResult.AnimSequences,
			&OutResult.AnimSequences
		))
		{
			UE_LOG(
				"FBX import failed: animation save failed. Source=%s Target=%s",
				Request.SourceFbxPath.c_str(),
				EffectiveSkeletonPath.c_str()
			);
			return false;
		}
	}

	if (Request.bImportSkin)
	{
		ScanMeshAssets();
		FMaterialManager::Get().ScanMaterialAssets();
	}
	if (Request.bImportSkeleton)
	{
		FSkeletonManager::Get().ScanSkeletonAssets();
	}
	if (Request.bImportAnimations)
	{
		FAnimationManager::Get().RefreshAvailableAnimations();
	}

	return true;
}
