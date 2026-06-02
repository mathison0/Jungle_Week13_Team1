#include "SkeletalMesh.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/ReferenceCollector.h"
#include "Serialization/Archive.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Physics/PhysicsAssetManager.h"
#include "Core/Types/EngineTypes.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	constexpr float MinGeneratedBodyLength = 1.0f;
	constexpr float GeneratedCapsuleRadiusScale = 0.16f;
	constexpr float GeneratedCapsuleLengthScale = 0.65f;
	constexpr float GeneratedLeafSphereScale = 0.025f;
	constexpr float GeneratedMinRadius = 1.0f;

	FVector GetMatrixLocation(const FMatrix& Matrix)
	{
		return Matrix.GetLocation();
	}

	FVector TransformPositionByInverse(const FMatrix& Matrix, const FVector& Position)
	{
		return Matrix.GetInverse().TransformPositionWithW(Position);
	}

	FQuat MakeQuatFromZAxis(const FVector& InDirection)
	{
		FVector Direction = InDirection;
		if (Direction.IsNearlyZero())
		{
			return FQuat::Identity;
		}
		Direction.Normalize();

		const FVector Up = FVector::UpVector;
		const float Dot = std::clamp(Up.Dot(Direction), -1.0f, 1.0f);
		if (Dot > 0.9999f)
		{
			return FQuat::Identity;
		}
		if (Dot < -0.9999f)
		{
			return FQuat::FromAxisAngle(FVector::XAxisVector, FMath::Pi);
		}

		FVector Axis = Up.Cross(Direction);
		Axis.Normalize();
		return FQuat::FromAxisAngle(Axis, std::acos(Dot)).GetNormalized();
	}

	float ComputeFallbackBodyRadius(const FSkeletalMesh* Asset)
	{
		if (!Asset || Asset->Vertices.empty())
		{
			return GeneratedMinRadius;
		}

		FBoundingBox Bounds;
		for (const FVertexPNCTBW& Vertex : Asset->Vertices)
		{
			Bounds.Expand(Vertex.Position);
		}

		if (!Bounds.IsValid())
		{
			return GeneratedMinRadius;
		}

		return std::max(GeneratedMinRadius, Bounds.GetExtent().Length() * GeneratedLeafSphereScale);
	}

	int32 FindBoneIndexByName(const FSkeletalMesh* Asset, const FName& BoneName)
	{
		if (!Asset)
		{
			return -1;
		}

		const FString BoneNameString = BoneName.ToString();
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (Asset->Bones[BoneIndex].Name == BoneNameString)
			{
				return BoneIndex;
			}
		}
		return -1;
	}

	FTransform MakeConstraintFrame(const FMatrix& BodyGlobalPose, const FVector& JointGlobalLocation)
	{
		FTransform Frame;
		Frame.Location = TransformPositionByInverse(BodyGlobalPose, JointGlobalLocation);
		Frame.Rotation = FQuat::Identity;
		Frame.Scale = FVector::OneVector;
		return Frame;
	}
}


void USkeletalMesh::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading() && !SkeletalMeshAsset)
	{
		SkeletalMeshAsset = new FSkeletalMesh();
	}

    if (Ar.IsSaving())
    {
        SyncSkeletonBindingToAsset();
    }

	Ar << SkeletalMeshAsset->PathFileName;
	Ar << SkeletalMeshAsset->SkeletonPath;
    Ar << SkeletalMeshAsset->SkeletonAssetGuid;
    Ar << SkeletalMeshAsset->SkeletonCompatibilitySignature;
	Ar << SkeletalMeshAsset->Vertices;
	Ar << SkeletalMeshAsset->Indices;
	Ar << SkeletalMeshAsset->Sections;
	Ar << SkeletalMeshAsset->MeshRanges;
	Ar << SkeletalMeshAsset->Bones;
	Ar << SkeletalMaterials;
	Ar << SkeletalMeshAsset->MorphTargets;

	SerializeProperties(Ar, PF_Save);

	if (Ar.IsLoading())
	{
		SkeletalMeshAsset->NormalizeBonePoseData();
        SyncSkeletonBindingFromAsset();
		CacheSectionMaterialIndices();
		SkeletalMeshAsset->bBoundsValid = false;
		// PhysicsAsset은 즉시 로드하지 않는다 — 썸네일/프리뷰는 물리를 안 쓰므로 첫 GetPhysicsAsset/EnsurePhysicsAsset 때 1회 lazy-load.
		PhysicsAsset = nullptr;
		bPhysicsAssetLoadAttempted = false;
	}
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InMesh)
{
	SkeletalMeshAsset = InMesh;
	if (SkeletalMeshAsset)
	{
		SkeletalMeshAsset->NormalizeBonePoseData();
	}
    SyncSkeletonBindingFromAsset();
	CacheSectionMaterialIndices();
}

FSkeletalMesh* USkeletalMesh::GetSkeletalMeshAsset() const
{
	return SkeletalMeshAsset;
}

UPhysicsAsset* USkeletalMesh::EnsurePhysicsAsset()
{
	EnsurePhysicsAssetLoaded(); // 저장된 에셋을 먼저 lazy-load한 뒤, 없을 때만 새로 만든다.
	if (!PhysicsAsset)
	{
		PhysicsAsset = UObjectManager::Get().CreateObject<UPhysicsAsset>();
		bPhysicsAssetLoadAttempted = true;
	}

	return PhysicsAsset;
}

bool USkeletalMesh::GenerateDefaultPhysicsAsset(bool bOverwriteExisting)
{
	if (!SkeletalMeshAsset || SkeletalMeshAsset->Bones.empty())
	{
		return false;
	}

	UPhysicsAsset* Asset = EnsurePhysicsAsset();
	if (!Asset)
	{
		return false;
	}

	if (!bOverwriteExisting && Asset->HasAnyBodySetup())
	{
		return false;
	}

	if (bOverwriteExisting)
	{
		for (UBodySetup* BodySetup : Asset->BodySetups)
		{
			if (BodySetup)
			{
				UObjectManager::Get().DestroyObject(BodySetup);
			}
		}
		Asset->BodySetups.clear();
		Asset->ConstraintSetups.clear();
	}

	const TArray<FBone>& Bones = SkeletalMeshAsset->Bones;
	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
	{
		AddDefaultPhysicsBodyForBone(BoneIndex);
	}

	for (int32 ChildBoneIndex = 0; ChildBoneIndex < static_cast<int32>(Bones.size()); ++ChildBoneIndex)
	{
		const int32 ParentBoneIndex = Bones[ChildBoneIndex].ParentIndex;
		if (ParentBoneIndex < 0 || ParentBoneIndex >= static_cast<int32>(Bones.size()))
		{
			continue;
		}

		AddPhysicsConstraintBetweenBodies(FName(Bones[ParentBoneIndex].Name), FName(Bones[ChildBoneIndex].Name));
	}

	return Asset->HasAnyBodySetup();
}

UBodySetup* USkeletalMesh::AddDefaultPhysicsBodyForBone(int32 BoneIndex)
{
	if (!SkeletalMeshAsset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(SkeletalMeshAsset->Bones.size()))
	{
		return nullptr;
	}

	UPhysicsAsset* Asset = EnsurePhysicsAsset();
	if (!Asset)
	{
		return nullptr;
	}

	const TArray<FBone>& Bones = SkeletalMeshAsset->Bones;
	const FBone& Bone = Bones[BoneIndex];
	const FName BoneName(Bone.Name);
	if (Asset->FindBodySetupByBoneName(BoneName))
	{
		return nullptr;
	}

	int32 FirstChildIndex = -1;
	for (int32 CandidateIndex = 0; CandidateIndex < static_cast<int32>(Bones.size()); ++CandidateIndex)
	{
		if (Bones[CandidateIndex].ParentIndex == BoneIndex)
		{
			FirstChildIndex = CandidateIndex;
			break;
		}
	}

	UBodySetup* BodySetup = UObjectManager::Get().CreateObject<UBodySetup>(Asset);
	if (!BodySetup)
	{
		return nullptr;
	}

	BodySetup->SetBoneName(BoneName);
	BodySetup->SetFName(FName(Bone.Name + "_BodySetup"));
	FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();

	if (FirstChildIndex >= 0)
	{
		const FVector BonePos = GetMatrixLocation(Bone.GetReferenceGlobalPose());
		const FVector ChildPos = GetMatrixLocation(Bones[FirstChildIndex].GetReferenceGlobalPose());
		const FVector ChildLocalPos = TransformPositionByInverse(Bone.GetReferenceGlobalPose(), ChildPos);
		const float Distance = (ChildPos - BonePos).Length();

		if (Distance >= MinGeneratedBodyLength)
		{
			FKSphylElem Capsule;
			Capsule.Name = Bone.Name + "_Body";
			Capsule.Radius = std::max(GeneratedMinRadius, Distance * GeneratedCapsuleRadiusScale);
			Capsule.Length = std::max(0.0f, Distance * GeneratedCapsuleLengthScale);
			Capsule.Transform.Location = ChildLocalPos * 0.5f;
			Capsule.Transform.Rotation = MakeQuatFromZAxis(ChildLocalPos);
			Capsule.Transform.Scale = FVector::OneVector;
			AggGeom.SphylElems.push_back(Capsule);
		}
	}

	if (AggGeom.IsEmpty())
	{
		FKSphereElem Sphere;
		Sphere.Name = Bone.Name + "_Body";
		Sphere.Radius = ComputeFallbackBodyRadius(SkeletalMeshAsset);
		Sphere.Transform.Location = FVector::ZeroVector;
		Sphere.Transform.Rotation = FQuat::Identity;
		Sphere.Transform.Scale = FVector::OneVector;
		AggGeom.SphereElems.push_back(Sphere);
	}

	Asset->BodySetups.push_back(BodySetup);
	return BodySetup;
}

bool USkeletalMesh::AddPhysicsConstraintBetweenBodies(const FName& ParentBoneName, const FName& ChildBoneName)
{
	if (!SkeletalMeshAsset || ParentBoneName == ChildBoneName)
	{
		return false;
	}

	UPhysicsAsset* Asset = EnsurePhysicsAsset();
	if (!Asset)
	{
		return false;
	}

	if (!Asset->FindBodySetupByBoneName(ParentBoneName) || !Asset->FindBodySetupByBoneName(ChildBoneName))
	{
		return false;
	}

	const int32 ParentBoneIndex = FindBoneIndexByName(SkeletalMeshAsset, ParentBoneName);
	const int32 ChildBoneIndex = FindBoneIndexByName(SkeletalMeshAsset, ChildBoneName);
	if (ParentBoneIndex < 0 || ChildBoneIndex < 0)
	{
		return false;
	}

	const TArray<FBone>& Bones = SkeletalMeshAsset->Bones;
	if (Bones[ChildBoneIndex].ParentIndex != ParentBoneIndex)
	{
		return false;
	}

	if (HasPhysicsConstraintBetweenBodies(ParentBoneName, ChildBoneName))
	{
		return false;
	}

	const FVector JointLocation = GetMatrixLocation(Bones[ChildBoneIndex].GetReferenceGlobalPose());

	FConstraintInstance Constraint;
	FConstraintSetup& Setup = Constraint.Setup;
	Setup.ConstraintName = ParentBoneName.ToString() + "_To_" + ChildBoneName.ToString() + "_Constraint";
	Setup.ParentBoneName = ParentBoneName;
	Setup.ChildBoneName = ChildBoneName;
	Setup.ParentFrame = MakeConstraintFrame(Bones[ParentBoneIndex].GetReferenceGlobalPose(), JointLocation);
	Setup.ChildFrame = MakeConstraintFrame(Bones[ChildBoneIndex].GetReferenceGlobalPose(), JointLocation);
	Asset->ConstraintSetups.push_back(Setup);
	return true;
}

bool USkeletalMesh::HasPhysicsConstraintBetweenBodies(const FName& BoneNameA, const FName& BoneNameB) const
{
	const UPhysicsAsset* Asset = PhysicsAsset;
	if (!Asset)
	{
		return false;
	}

	for (const FConstraintSetup& Constraint : Asset->ConstraintSetups)
	{
		const bool bSameDirection = Constraint.ParentBoneName == BoneNameA && Constraint.ChildBoneName == BoneNameB;
		const bool bOppositeDirection = Constraint.ParentBoneName == BoneNameB && Constraint.ChildBoneName == BoneNameA;
		if (bSameDirection || bOppositeDirection)
		{
			return true;
		}
	}
	return false;
}
void USkeletalMesh::SetPhysicsAsset(UPhysicsAsset* InPhysicsAsset)
{
	PhysicsAsset = InPhysicsAsset;
	bPhysicsAssetLoadAttempted = true; // 명시 설정 후엔 lazy-load가 덮어쓰지 않도록
	if (PhysicsAsset)
	{
		PhysicsAssetPath = PhysicsAsset->GetAssetPathFileName();
	}
	else
	{
		PhysicsAssetPath = "None";
	}
	PhysicsAssetPath.SetCachedObject(PhysicsAsset);
}

void USkeletalMesh::AddReferencedObjects(FReferenceCollector& Collector)
{
	UObject::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(PhysicsAsset);
}

void USkeletalMesh::PostEditProperty(const char* PropertyName)
{
	UObject::PostEditProperty(PropertyName);
	if (PropertyName && std::strcmp(PropertyName, "PhysicsAssetPath") == 0)
	{
		LoadPhysicsAssetFromPath();
	}
}

void USkeletalMesh::LoadPhysicsAssetFromPath()
{
	PhysicsAsset = nullptr;
	if (!PhysicsAssetPath.IsNull())
	{
		PhysicsAsset = FPhysicsAssetManager::Get().Load(PhysicsAssetPath.ToString());
	}
	PhysicsAssetPath.SetCachedObject(PhysicsAsset);
	bPhysicsAssetLoadAttempted = true;
}

// PhysicsAsset lazy-load 진입점. 아직 시도 안 했으면 저장 경로에서 1회 로드한다.
// 썸네일/프리뷰는 GetPhysicsAsset을 호출하지 않으므로 물리 에셋을 영영 로드하지 않아 로드 비용이 사라진다.
void USkeletalMesh::EnsurePhysicsAssetLoaded()
{
	if (!bPhysicsAssetLoadAttempted)
	{
		LoadPhysicsAssetFromPath();
	}
}

UPhysicsAsset* USkeletalMesh::GetPhysicsAsset() const
{
	const_cast<USkeletalMesh*>(this)->EnsurePhysicsAssetLoaded();
	return PhysicsAsset;
}

void USkeletalMesh::SetSkeletalMaterials(TArray<FSkeletalMaterial>&& InMaterials)
{
	SkeletalMaterials = InMaterials;
	CacheSectionMaterialIndices();
}

const TArray<FSkeletalMaterial>& USkeletalMesh::GetSkeletalMaterials() const
{
	return SkeletalMaterials;
}

void USkeletalMesh::InitResources(ID3D11Device* InDevice)
{
	if (!InDevice || !SkeletalMeshAsset) return;

	const uint32 CPUSize =
		static_cast<uint32>(SkeletalMeshAsset->Vertices.size() * sizeof(FVertexPNCTBW)) +
		static_cast<uint32>(SkeletalMeshAsset->Indices.size() * sizeof(uint32));
	MemoryStats::AddSkeletalMeshCPUMemory(CPUSize);

	TMeshData<FVertexPNCTBW> RenderMeshData;
	RenderMeshData.Vertices.reserve(SkeletalMeshAsset->Vertices.size());

	for (const FVertexPNCTBW& RawVert : SkeletalMeshAsset->Vertices)
	{
		FVertexPNCTBW RenderVert;
		RenderVert.Position = RawVert.Position;
		RenderVert.Normal = RawVert.Normal;
		RenderVert.Color = RawVert.Color;
		RenderVert.UV = RawVert.UV;
		RenderVert.Tangent = RawVert.Tangent;
		std::copy(std::begin(RawVert.BoneIndices), std::end(RawVert.BoneIndices), std::begin(RenderVert.BoneIndices));
		std::copy(std::begin(RawVert.BoneWeights), std::end(RawVert.BoneWeights), std::begin(RenderVert.BoneWeights));
		RenderMeshData.Vertices.push_back(RenderVert);
	}
	RenderMeshData.Indices = SkeletalMeshAsset->Indices;

	SkeletalMeshAsset->RenderBuffer = std::make_unique<FMeshBuffer>();
	SkeletalMeshAsset->RenderBuffer->Create(InDevice, RenderMeshData);
}

void USkeletalMesh::SetSkeleton(USkeleton* InSkeleton)
{
	Skeleton = InSkeleton;

	if (Skeleton)
	{
        SetSkeletonBinding(Skeleton->GetSkeletonBinding());
	}
	else
	{
        FSkeletonBinding EmptyBinding;
        SetSkeletonBinding(EmptyBinding);
	}
}

USkeleton* USkeletalMesh::GetSkeleton() const
{
	return Skeleton;
}

void USkeletalMesh::SetSkeletonBinding(const FSkeletonBinding& InBinding)
{
    SkeletonBinding = InBinding;
    if (SkeletonBinding.SkeletonPath.empty())
    {
        SkeletonBinding.SkeletonPath = "None";
    }
    SyncSkeletonBindingToAsset();
}

void USkeletalMesh::SyncSkeletonBindingToAsset()
{
    if (!SkeletalMeshAsset)
    {
        return;
    }

    SkeletalMeshAsset->SkeletonPath = SkeletonBinding.SkeletonPath.empty() ? FString("None") : SkeletonBinding.SkeletonPath;
    SkeletalMeshAsset->SkeletonAssetGuid = SkeletonBinding.SkeletonAssetGuid;
    SkeletalMeshAsset->SkeletonCompatibilitySignature = SkeletonBinding.CompatibilitySignature;
}

void USkeletalMesh::SyncSkeletonBindingFromAsset()
{
    if (!SkeletalMeshAsset)
    {
        return;
    }

    SkeletonBinding.SkeletonPath = SkeletalMeshAsset->SkeletonPath.empty() ? FString("None") : SkeletalMeshAsset->SkeletonPath;
    SkeletonBinding.SkeletonAssetGuid = SkeletalMeshAsset->SkeletonAssetGuid;
    SkeletonBinding.CompatibilitySignature = SkeletalMeshAsset->SkeletonCompatibilitySignature;
}

void USkeletalMesh::CacheSectionMaterialIndices()
{
	if (!SkeletalMeshAsset)
	{
		return;
	}

	for (FSkeletalMeshSection& Section : SkeletalMeshAsset->Sections)
	{
		Section.MaterialIndex = -1;
		for (int32 i = 0; i < static_cast<int32>(SkeletalMaterials.size()); ++i)
		{
			if (SkeletalMaterials[i].MaterialSlotName == Section.MaterialSlotName)
			{
				Section.MaterialIndex = i;
				break;
			}
		}
	}
}
