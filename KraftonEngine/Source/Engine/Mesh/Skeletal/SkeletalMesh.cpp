#include "SkeletalMesh.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Serialization/Archive.h"
#include "Animation/Skeleton/Skeleton.h"
#include "Core/Types/EngineTypes.h"
#include "Math/MathUtils.h"

#include <algorithm>
#include <cmath>

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

	if (Ar.IsSaving())
	{
		EnsurePhysicsAsset();
	}
	SerializeProperties(Ar, PF_Save);
	if (PhysicsAsset)
	{
		PhysicsAsset->SetOuter(this);
	}

	if (Ar.IsLoading())
	{
		SkeletalMeshAsset->NormalizeBonePoseData();
        SyncSkeletonBindingFromAsset();
		CacheSectionMaterialIndices();
		SkeletalMeshAsset->bBoundsValid = false;
		EnsurePhysicsAsset();
	}
}

void USkeletalMesh::SetSkeletalMeshAsset(FSkeletalMesh* InMesh)
{
	SkeletalMeshAsset = InMesh;
	EnsurePhysicsAsset();
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
	if (!PhysicsAsset)
	{
		PhysicsAsset = UObjectManager::Get().CreateObject<UPhysicsAsset>(this);
	}
	else
	{
		PhysicsAsset->SetOuter(this);
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
	const float FallbackRadius = ComputeFallbackBodyRadius(SkeletalMeshAsset);

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Bones.size()); ++BoneIndex)
	{
		const FBone& Bone = Bones[BoneIndex];

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
			continue;
		}

		BodySetup->SetBoneName(FName(Bone.Name));
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
			Sphere.Radius = FallbackRadius;
			Sphere.Transform.Location = FVector::ZeroVector;
			Sphere.Transform.Rotation = FQuat::Identity;
			Sphere.Transform.Scale = FVector::OneVector;
			AggGeom.SphereElems.push_back(Sphere);
		}

		Asset->BodySetups.push_back(BodySetup);
	}

	return Asset->HasAnyBodySetup();
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
