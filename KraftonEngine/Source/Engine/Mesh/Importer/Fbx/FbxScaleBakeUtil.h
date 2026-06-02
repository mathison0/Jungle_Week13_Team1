#pragma once

#include "Core/Types/CoreTypes.h"
#include "Math/Transform.h"

struct FBone;
struct FReferenceSkeleton;

// ======================================================
// FbxScaleBakeUtil
// FBX 본 bind pose에 박힌 "균등 스케일 S"를 import 단계에서 베이크아웃해
// 모든 본의 bind scale을 1로 만든다. 버텍스/렌더 결과는 불변이고,
// 물리·랙돌·PhysicsAsset이 암묵적으로 가정하는 "본 scale=1" 전제를 충족시킨다.
//
// 동작: 본별 bind global의 스케일을 제거(행 정규화, translation 유지)하고
//       그로부터 local/inverse-bind를 재계산한다. 애니메이션 키프레임도
//       같은 규칙(translation *= 부모 누적 스케일, scale=1)으로 맞춰야
//       스케일된 릭의 애니가 깨지지 않는다.
//
// 가드: 본 스케일이 비균등(shear 위험)이거나 이미 1이면 베이크하지 않는다.
// ======================================================
struct FScaleBakeResult
{
	bool          bBaked = false;            // 베이크 결정(균등 + 1이 아님)일 때 true
	TArray<float> ScaleAccum;                // 본별 원본 bind global 균등 스케일(애니 베이크용)
	float         MaxScale = 1.0f;           // 최대 본 스케일(진단/로그용)
	float         MaxNonUniformity = 0.0f;   // 본 내 축 간 최대 편차 비율(비균등 탐지)
};

namespace FbxScaleBakeUtil
{
	// SkeletalMesh의 FBone 배열에 적용. bApplyMutation=false면 측정만 하고 본 데이터를 건드리지 않는다.
	FScaleBakeResult BakeOutBindScale(TArray<FBone>& Bones, bool bApplyMutation, float UniformTolerance = 1.0e-3f);

	// Skeleton asset의 FReferenceSkeleton에 적용(skeleton-only / animation-only import 경로용).
	FScaleBakeResult BakeOutBindScale(FReferenceSkeleton& Skeleton, bool bApplyMutation, float UniformTolerance = 1.0e-3f);

	// 애니메이션 키프레임 local transform 베이크: scale→1, translation *= ParentScaleAccum, rotation 불변.
	// 베이크된 스켈레톤(ScaleAccum)과 짝을 이뤄야 한다.
	FTransform BakeOutLocalTransform(const FTransform& Local, float ParentScaleAccum);
}
