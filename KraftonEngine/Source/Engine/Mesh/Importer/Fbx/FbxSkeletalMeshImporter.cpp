#include "Mesh/Importer/Fbx/FbxSkeletalMeshImporter.h"
#include "Mesh/Importer/Fbx/FbxSceneQuery.h"
#include "Mesh/Importer/Fbx/FbxMaterialImporter.h"
#include "Mesh/Importer/Fbx/FbxSkeletonImporter.h"
#include "Mesh/Importer/Fbx/FbxSkinWeightImporter.h"
#include "Mesh/Importer/Fbx/FbxAnimationImporter.h"
#include "Mesh/Importer/Fbx/FbxScaleBakeUtil.h"
#include "Core/Logging/Log.h"

#include <utility>

namespace
{
	static bool ImportMeshCore(
		FbxScene*                         Scene,
		FFbxImportContext&                Context,
		FSkeletalMesh&                    OutMesh,
		TArray<FSkeletalMaterial>&        OutMaterials,
		FReferenceSkeleton&               OutSourceSkeleton,
		TArray<FFbxImportedMaterialInfo>& OutSourceMaterials,
		FString*                          OutMessage
		)
	{
		FbxNode* RootNode = Scene ? Scene->GetRootNode() : nullptr;
		if (!RootNode)
		{
			if (OutMessage) *OutMessage = "FBX skeletal mesh import failed: root node not found.";
			return false;
		}

		Context.AllNodes.clear();
		Context.MeshNodes.clear();
		Context.AnimSequences.clear();
		FFbxSceneQuery::CollectAllNodes(RootNode, Context.AllNodes);
		FFbxSceneQuery::CollectMeshNodes(RootNode, Context.MeshNodes);

		FFbxMaterialImporter::CollectMaterials(Scene, Context);

		if (!FFbxSkeletonImporter::ImportSkeleton(Scene, Context, OutMessage))
		{
			return false;
		}

		if (!FFbxSkinWeightImporter::ImportSkin(Scene, Context, OutMessage))
		{
			return false;
		}

		// [Phase 1a] 본 bind scale 측정만(mutate 없음). Yeoul≈15.82 / Furina≈1.0 확인용. Phase 1b에서 실제 베이크로 전환.
		{
			const FScaleBakeResult ScaleProbe = FbxScaleBakeUtil::BakeOutBindScale(Context.Bones, /*bApplyMutation*/ false);
			UE_LOG("[ScaleBake] probe bones=%d maxScale=%.4f nonUniformity=%.4f wouldBake=%d",
				static_cast<int32>(Context.Bones.size()), ScaleProbe.MaxScale, ScaleProbe.MaxNonUniformity, ScaleProbe.bBaked ? 1 : 0);
		}

		// Skin import can refine inverse bind poses from FBX clusters, so rebuild the reference skeleton after skin data is processed.
		Context.ReferenceSkeleton.Bones.clear();
		Context.ReferenceSkeleton.Bones.reserve(Context.Bones.size());
		for (const FBone& Bone : Context.Bones)
		{
			FReferenceBone RefBone;
			RefBone.Name            = Bone.Name;
			RefBone.ParentIndex     = Bone.ParentIndex;
			RefBone.LocalBindPose   = Bone.GetReferenceLocalPose();
			RefBone.GlobalBindPose  = Bone.GetReferenceGlobalPose();
			RefBone.InverseBindPose = Bone.GetInverseBindPose();
			Context.ReferenceSkeleton.Bones.push_back(RefBone);
		}

		OutMesh.Vertices     = std::move(Context.SkeletalVertices);
		OutMesh.Indices      = std::move(Context.SkeletalIndices);
		OutMesh.Sections     = std::move(Context.SkeletalSections);
		OutMesh.MeshRanges   = std::move(Context.SkeletalMeshRanges);
		OutMesh.MorphTargets = std::move(Context.SkeletalMorphTargets);
		OutMesh.Bones        = Context.Bones;
		OutMesh.PathFileName = Context.SourcePath;

		OutSourceSkeleton  = Context.ReferenceSkeleton;
		OutSourceMaterials = Context.Materials;
		FFbxMaterialImporter::BuildSkeletalMaterials(Context, OutMesh.Sections, OutMaterials, OutMesh.Sections);
		return true;
	}
}

bool FFbxSkeletalMeshImporter::Import(FbxScene* Scene, FFbxImportContext& Context, FFbxSkeletalMeshImportResult& OutResult, FString* OutMessage)
{
	OutResult = FFbxSkeletalMeshImportResult();

	if (!ImportMeshCore(Scene, Context, OutResult.Mesh, OutResult.Materials, OutResult.Skeleton, OutResult.SourceMaterials, OutMessage))
	{
		return false;
	}

	if (!FFbxAnimationImporter::ImportAnimations(Scene, Context, nullptr, OutMessage))
	{
		return false;
	}

	OutResult.AnimSequences = std::move(Context.AnimSequences);
	return true;
}

bool FFbxSkeletalMeshImporter::ImportMeshOnly(FbxScene* Scene, FFbxImportContext& Context, FFbxSkeletalMeshOnlyImportResult& OutResult, FString* OutMessage)
{
	OutResult = FFbxSkeletalMeshOnlyImportResult();
	return ImportMeshCore(Scene, Context, OutResult.Mesh, OutResult.Materials, OutResult.SourceSkeleton, OutResult.SourceMaterials, OutMessage);
}
