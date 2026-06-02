#include "Mesh/Importer/Fbx/FbxScaleBakeUtil.h"

#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Animation/Skeleton/SkeletonTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

#include <algorithm>
#include <cmath>

namespace
{
	float RowLength(const FMatrix& M, int32 Row)
	{
		return std::sqrt(M.M[Row][0] * M.M[Row][0] + M.M[Row][1] * M.M[Row][1] + M.M[Row][2] * M.M[Row][2]);
	}

	// 행 0/1/2를 정규화해 스케일을 제거하고, translation(행 3)은 그대로 둔 affine 행렬을 만든다.
	// 균등 스케일 + 회전이면 순수 회전 행렬이 남는다.
	FMatrix RemoveScaleKeepTranslation(const FMatrix& M)
	{
		FMatrix R = FMatrix::Identity;
		for (int32 Row = 0; Row < 3; ++Row)
		{
			const float Len = RowLength(M, Row);
			const float Inv = (Len > 1.0e-8f) ? (1.0f / Len) : 0.0f;
			R.M[Row][0] = M.M[Row][0] * Inv;
			R.M[Row][1] = M.M[Row][1] * Inv;
			R.M[Row][2] = M.M[Row][2] * Inv;
			R.M[Row][3] = 0.0f;
		}
		R.M[3][0] = M.M[3][0];
		R.M[3][1] = M.M[3][1];
		R.M[3][2] = M.M[3][2];
		R.M[3][3] = 1.0f;
		return R;
	}

	struct FBakeCore
	{
		FScaleBakeResult Info;
		TArray<FMatrix>  NewLocal;
		TArray<FMatrix>  NewGlobal;
		TArray<FMatrix>  NewInverse;
	};

	// bind global 배열 + parent 인덱스로부터 스케일을 측정하고(항상), bApply면 scale-free local/global/inverse를 만든다.
	// bone 배열은 parent-first 전제. NewGlobal은 본별 독립이라 순서 무관, NewLocal은 부모의 NewGlobal만 참조한다.
	FBakeCore RunBakeCore(const TArray<FMatrix>& Globals, const TArray<int32>& Parents, bool bApply, float Tol)
	{
		FBakeCore Out;
		const int32 N = static_cast<int32>(Globals.size());
		Out.Info.ScaleAccum.assign(N, 1.0f);

		float MaxDevFrom1 = 0.0f;
		for (int32 b = 0; b < N; ++b)
		{
			const FVector S = Globals[b].GetScale();
			const float Mx = std::max({ S.X, S.Y, S.Z });
			const float Mn = std::min({ S.X, S.Y, S.Z });
			const float Avg = (S.X + S.Y + S.Z) / 3.0f;
			const float NonUnif = (Mx > 1.0e-6f) ? (Mx - Mn) / Mx : 0.0f;

			Out.Info.ScaleAccum[b] = Avg;
			Out.Info.MaxScale = std::max(Out.Info.MaxScale, Avg);
			Out.Info.MaxNonUniformity = std::max(Out.Info.MaxNonUniformity, NonUnif);
			MaxDevFrom1 = std::max(MaxDevFrom1, std::fabs(Avg - 1.0f));
		}

		const bool bUniform  = Out.Info.MaxNonUniformity <= Tol;  // 비균등이면 shear 위험 → 베이크 안 함
		const bool bHasScale = MaxDevFrom1 > Tol;                 // 이미 1이면 no-op
		Out.Info.bBaked = bUniform && bHasScale;

		if (!bApply || !Out.Info.bBaked)
		{
			return Out;
		}

		Out.NewGlobal.resize(N);
		Out.NewLocal.resize(N);
		Out.NewInverse.resize(N);
		for (int32 b = 0; b < N; ++b)
		{
			Out.NewGlobal[b]  = RemoveScaleKeepTranslation(Globals[b]);
			Out.NewInverse[b] = Out.NewGlobal[b].GetInverse();
		}
		for (int32 b = 0; b < N; ++b)
		{
			const int32 P = Parents[b];
			// 부모의 역행렬(NewInverse[P])을 재사용 — 본당 역행렬 1회로 부모 중복 계산 제거.
			Out.NewLocal[b] = (P >= 0 && P < N) ? Out.NewGlobal[b] * Out.NewInverse[P] : Out.NewGlobal[b];
		}
		return Out;
	}
}

FScaleBakeResult FbxScaleBakeUtil::BakeOutBindScale(TArray<FBone>& Bones, bool bApplyMutation, float UniformTolerance)
{
	const int32 N = static_cast<int32>(Bones.size());
	TArray<FMatrix> Globals; Globals.reserve(N);
	TArray<int32>   Parents; Parents.reserve(N);
	for (const FBone& Bone : Bones)
	{
		Globals.push_back(Bone.GetSkinBindGlobalPose());
		Parents.push_back(Bone.ParentIndex);
	}

	FBakeCore Core = RunBakeCore(Globals, Parents, bApplyMutation, UniformTolerance);

	if (bApplyMutation && Core.Info.bBaked)
	{
		for (int32 b = 0; b < N; ++b)
		{
			FBone& Bone = Bones[b];
			Bone.LocalMatrix           = Core.NewLocal[b];
			Bone.GlobalMatrix          = Core.NewGlobal[b];
			Bone.InverseBindPoseMatrix = Core.NewInverse[b];
			Bone.ReferenceLocalPose    = Core.NewLocal[b];
			Bone.ReferenceGlobalPose   = Core.NewGlobal[b];
			Bone.SkinBindGlobalPose    = Core.NewGlobal[b];
		}
	}
	return Core.Info;
}

FScaleBakeResult FbxScaleBakeUtil::BakeOutBindScale(FReferenceSkeleton& Skeleton, bool bApplyMutation, float UniformTolerance)
{
	const int32 N = Skeleton.GetNumBones();
	TArray<FMatrix> Globals; Globals.reserve(N);
	TArray<int32>   Parents; Parents.reserve(N);
	for (const FReferenceBone& Bone : Skeleton.Bones)
	{
		Globals.push_back(Bone.GlobalBindPose);
		Parents.push_back(Bone.ParentIndex);
	}

	FBakeCore Core = RunBakeCore(Globals, Parents, bApplyMutation, UniformTolerance);

	if (bApplyMutation && Core.Info.bBaked)
	{
		for (int32 b = 0; b < N; ++b)
		{
			FReferenceBone& Bone = Skeleton.Bones[b];
			Bone.LocalBindPose   = Core.NewLocal[b];
			Bone.GlobalBindPose  = Core.NewGlobal[b];
			Bone.InverseBindPose = Core.NewInverse[b];
		}
	}
	return Core.Info;
}

FTransform FbxScaleBakeUtil::BakeOutLocalTransform(const FTransform& Local, float ParentScaleAccum)
{
	FTransform Result = Local;
	Result.Location = Local.Location * ParentScaleAccum;
	Result.Scale    = FVector::OneVector;
	return Result;
}
