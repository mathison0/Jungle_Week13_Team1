#pragma once

#include "Mesh/Importer/Fbx/FbxImportTypes.h"
#include "Render/Types/VertexTypes.h"

#include <fbxsdk.h>

struct FFbxMorphVertexSource
{
	FbxMesh* Mesh                    = nullptr;
	int32    ControlPointIndex       = -1;
	FMatrix  MeshBindGlobal          = FMatrix::Identity;
	FMatrix  RigidBindCorrection     = FMatrix::Identity;
	bool     bUseRigidBindCorrection = false;
};

struct FFbxImportContext
{
	FString SourcePath;

	TArray<FbxNode*> AllNodes;
	TArray<FbxNode*> MeshNodes;

	TArray<FFbxImportedMaterialInfo> Materials;
	TMap<FbxSurfaceMaterial*, int32> MaterialToSlotIndex;

	TArray<FBone> Bones;
	TMap<FbxNode*, int32> BoneNodeToIndex;
	FReferenceSkeleton ReferenceSkeleton;

	// [스케일 베이크아웃] import에서 본 bind 스케일을 1로 정규화했는지 + 본별 원본 누적 스케일.
	// 애니 키프레임을 베이크된 스켈레톤과 같은 scale-free 공간으로 맞추는 데 사용한다.
	bool          bBindScaleBaked = false;
	TArray<float> BindScaleAccum;

	TArray<FVertexPNCTBW>         SkeletalVertices;
	TArray<uint32>                SkeletalIndices;
	TArray<FSkeletalMeshSection>  SkeletalSections;
	TArray<FSkeletalMeshRange>    SkeletalMeshRanges;
	TArray<FMorphTarget>          SkeletalMorphTargets;
	TArray<FFbxMorphVertexSource> SkeletalMorphVertexSources;

	TArray<FVector> TangentSums;
	TArray<FVector> BitangentSums;

	TArray<UAnimSequence*> AnimSequences;
};
