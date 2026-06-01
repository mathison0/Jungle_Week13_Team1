#include "PhysXPhysicsScene.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
	constexpr float MatrixDecomposeTolerance = 1.0e-6f;

	float GetPhysicsAssetUniformScale(const FVector& WorldScale)
	{
		const float MaxAxisScale = std::max({ std::fabs(WorldScale.X), std::fabs(WorldScale.Y), std::fabs(WorldScale.Z) });
		return std::max(MaxAxisScale, 1.0e-4f);
	}

	FConstraintSetup MakeRuntimeConstraintSetup(const FConstraintSetup& Setup, float UniformScale)
	{
		FConstraintSetup RuntimeSetup = Setup;
		RuntimeSetup.ParentFrame.Location *= UniformScale;
		RuntimeSetup.ChildFrame.Location *= UniformScale;
		RuntimeSetup.ParentFrame.Scale = FVector::OneVector;
		RuntimeSetup.ChildFrame.Scale = FVector::OneVector;
		return RuntimeSetup;
	}

	FQuat ExtractRotationNoScale(const FMatrix& Matrix)
	{
		const FVector Scale = Matrix.GetScale();
		FMatrix RotationMatrix = Matrix;
		RotationMatrix.M[3][0] = 0.0f;
		RotationMatrix.M[3][1] = 0.0f;
		RotationMatrix.M[3][2] = 0.0f;
		RotationMatrix.M[3][3] = 1.0f;

		if (std::fabs(Scale.X) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[0][0] /= Scale.X;
			RotationMatrix.M[0][1] /= Scale.X;
			RotationMatrix.M[0][2] /= Scale.X;
		}

		if (std::fabs(Scale.Y) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[1][0] /= Scale.Y;
			RotationMatrix.M[1][1] /= Scale.Y;
			RotationMatrix.M[1][2] /= Scale.Y;
		}

		if (std::fabs(Scale.Z) > MatrixDecomposeTolerance)
		{
			RotationMatrix.M[2][0] /= Scale.Z;
			RotationMatrix.M[2][1] /= Scale.Z;
			RotationMatrix.M[2][2] /= Scale.Z;
		}

		return RotationMatrix.ToQuat().GetNormalized();
	}

	FTransform MakeComponentSpaceBodyTransform(
		const FVector& BodyWorldLocation,
		const FQuat& BodyWorldRotation,
		const FMatrix& ComponentWorldMatrix,
		const FMatrix& ComponentWorldInverse)
	{
		const FQuat ComponentWorldRotationInv = ExtractRotationNoScale(ComponentWorldMatrix).Inverse();
		return FTransform(
			ComponentWorldInverse.TransformPositionWithW(BodyWorldLocation),
			(BodyWorldRotation.GetNormalized() * ComponentWorldRotationInv).GetNormalized(),
			FVector::OneVector);
	}
}

bool FPhysXPhysicsScene::InstantiatePhysicsAssetBodies(USkeletalMeshComponent* Comp)
{
	if (!Comp || !Scene || !Physics || !DefaultMaterial) return false;

	// 재생성 시 기존 runtime body와 joint를 먼저 제거한다.
	// PhysicsAsset Editor에서 데이터를 수정한 뒤 다시 instantiate하는 경로도 이 함수를 사용한다.
	DestroyPhysicsAssetBodies(Comp);

	USkeletalMesh* SkeletalMesh = Comp->GetSkeletalMesh();
	UPhysicsAsset* PhysicsAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
	if (!PhysicsAsset || !PhysicsAsset->HasAnyBodySetup()) return false;

	const float UniformScale = GetPhysicsAssetUniformScale(Comp->GetWorldScale());

	TArray<std::unique_ptr<FBodyInstance>>& RuntimeBodies = Comp->GetBodies();
	RuntimeBodies.reserve(PhysicsAsset->BodySetups.size());

	for (int32 BodySetupIndex = 0; BodySetupIndex < static_cast<int32>(PhysicsAsset->BodySetups.size()); ++BodySetupIndex)
	{
		UBodySetup* BodySetup = PhysicsAsset->BodySetups[BodySetupIndex];
		if (!BodySetup || !BodySetup->HasGeometry()) continue;

		const FString BoneName = BodySetup->GetBoneName().ToString();
		const int32 BoneIndex = Comp->FindBoneIndex(BoneName);
		if (BoneIndex < 0)
		{
			UE_LOG("[PhysX] PhysicsAsset body skipped. Bone not found: %s", BoneName.c_str());
			continue;
		}

		FTransform BoneWorldTransform;
		if (!Comp->GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform)) continue;

		// refactor/PhysX-Core의 adapter를 통해 bone-local AggGeom을 독립 dynamic body로 만든다.
		// shape 생성 정책은 StaticMesh 경로와 같은 CreateShapeOnActor()를 공유한다.
		std::unique_ptr<FBodyInstance> RuntimeBody = CreateBodyFromBodySetup(Comp, BodySetup, BoneWorldTransform, true, UniformScale);
		if (!RuntimeBody) continue;

		RuntimeBody->SetBodyIndex(BodySetupIndex);
		RuntimeBody->SetBoneIndex(BoneIndex);
		RuntimeBody->SetSimulatePhysics(Comp->GetSimulatePhysics());
		RuntimeBodies.push_back(std::move(RuntimeBody));
	}

	// Editor에서 저장한 bone 이름으로 runtime body를 찾아 PxD6Joint를 생성한다.
	// BodySetup이 없거나 bone 이름이 틀린 constraint는 건너뛴다.
	for (const FConstraintSetup& ConstraintSetup : PhysicsAsset->ConstraintSetups)
	{
		FBodyInstance* ParentBody = nullptr;
		FBodyInstance* ChildBody = nullptr;
		const int32 ParentIndex = PhysicsAsset->FindBodySetupIndexByBoneName(ConstraintSetup.ParentBoneName);
		const int32 ChildIndex = PhysicsAsset->FindBodySetupIndexByBoneName(ConstraintSetup.ChildBoneName);

		for (auto& Body : RuntimeBodies)
		{
			if (!Body) continue;
			if (Body->GetBodyIndex() == ParentIndex) ParentBody = Body.get();
			if (Body->GetBodyIndex() == ChildIndex) ChildBody = Body.get();
		}

		if (!ParentBody || !ChildBody) continue;

		const FConstraintSetup RuntimeConstraintSetup = MakeRuntimeConstraintSetup(ConstraintSetup, UniformScale);
		if (std::unique_ptr<FConstraintInstance> Constraint = CreateConstraint(ParentBody, ChildBody, RuntimeConstraintSetup))
		{
			Comp->GetConstraints().push_back(std::move(Constraint));
		}
	}

	if (!RuntimeBodies.empty())
	{
		SkeletalPhysicsComponents.push_back(Comp);
		Comp->CachePhysicsAssetRuntimeScale();
		return true;
	}
	Comp->InvalidatePhysicsAssetRuntimeScale();
	return false;
}

void FPhysXPhysicsScene::DestroyPhysicsAssetBodies(USkeletalMeshComponent* Comp)
{
	if (!Comp) return;

	// PxJoint는 PxRigidActor를 참조하므로 body보다 먼저 제거해야 한다.
	// PxJoint 자원을 먼저 해제하고, unique_ptr 배열을 비워 객체를 삭제한다.
	for (auto& Constraint : Comp->GetConstraints())
	{
		if (Constraint) DestroyConstraint(Constraint.get());
	}
	Comp->GetConstraints().clear();

	// PhysX 자원을 먼저 해제(actor release)하고, 그 다음 unique_ptr 배열을 비워 객체를 삭제한다.
	for (auto& Body : Comp->GetBodies())
	{
		if (Body) ReleaseBodyResource(Body.get());
	}
	Comp->GetBodies().clear();
	Comp->InvalidatePhysicsAssetRuntimeScale();

	SkeletalPhysicsComponents.erase(
		std::remove(SkeletalPhysicsComponents.begin(), SkeletalPhysicsComponents.end(), Comp),
		SkeletalPhysicsComponents.end());
}

bool FPhysXPhysicsScene::SyncPhysicsAssetBodiesToComponentPose(USkeletalMeshComponent* Comp, bool bResetVelocity)
{
	if (!Comp) return false;

	bool bSynced = false;
	// Ragdoll 전환 순간의 animation pose를 PhysX body 시작 위치로 복사한다.
	// 이 과정을 생략하면 body가 bind pose나 이전 simulation 위치에서 시작해 튀어 보일 수 있다.
	for (auto& Body : Comp->GetBodies())
	{
		if (!Body || !Body->IsValidBodyInstance()) continue;

		FTransform BoneWorldTransform;
		if (!Comp->GetBoneWorldTransformByIndex(Body->GetBoneIndex(), BoneWorldTransform)) continue;

		Body->SetBodyTransform(BoneWorldTransform.Location, BoneWorldTransform.Rotation, bResetVelocity);
		bSynced = true;
	}
	return bSynced;
}

void FPhysXPhysicsScene::SetPhysicsAssetBodiesSimulate(USkeletalMeshComponent* Comp, bool bSimulate)
{
	if (!Comp) return;

	for (auto& Body : Comp->GetBodies())
	{
		if (!Body || !Body->IsValidBodyInstance()) continue;
		Body->SetSimulatePhysics(bSimulate);
		if (bSimulate) Body->WakeInstance();
	}
}

void FPhysXPhysicsScene::SyncPhysicsAssetBodiesToBones()
{
	for (USkeletalMeshComponent* Comp : SkeletalPhysicsComponents)
	{
		if (!Comp || !Comp->IsRagdollSimulating()) continue;

		USkeletalMesh* SkeletalMesh = Comp->GetSkeletalMesh();
		FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
		if (!Asset || Asset->Bones.empty()) continue;

		// collider가 없는 bone은 현재 pose를 유지하고, body가 있는 bone만 PhysX 결과로 교체한다.
		// 마지막에 전체 pose를 local transform으로 재계산하므로 손가락 같은 자식 bone도 부모를 따라간다.
		TArray<FMatrix> CurrentGlobalMatrices;
		Comp->GetCurrentBoneGlobalMatrices(CurrentGlobalMatrices);
		if (CurrentGlobalMatrices.size() != Asset->Bones.size()) continue;

		TArray<FMatrix> DesiredGlobalMatrices = CurrentGlobalMatrices;
		TArray<bool> HasBodyOverride(Asset->Bones.size(), false);
		const FMatrix& ComponentWorld = Comp->GetWorldMatrix();
		const FMatrix& ComponentWorldInv = Comp->GetWorldInverseMatrix();
		for (auto& Body : Comp->GetBodies())
		{
			if (!Body || !Body->IsValidBodyInstance()) continue;

			const int32 BoneIndex = Body->GetBoneIndex();
			if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size())) continue;

			const FTransform BodyComponentTransform = MakeComponentSpaceBodyTransform(
				Body->GetEngineWorldLocation(),
				Body->GetEngineWorldRotation(),
				ComponentWorld,
				ComponentWorldInv);
			// PhysX body에는 scale이 없으므로 위치와 회전을 분리해서 component-local로 변환한다.
			DesiredGlobalMatrices[BoneIndex] = BodyComponentTransform.ToMatrix();
			HasBodyOverride[BoneIndex] = true;
		}

		// 바디가 없는 본은 기존 로컬 자세를 유지하면서 시뮬레이션된 부모를 따라간다.
		// 이전에는 직전 글로벌 위치에 남아 있어 스킨이 아래로 길게 늘어났다.
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (HasBodyOverride[BoneIndex]) continue;

			const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
			const bool bHasParent = ParentIndex >= 0 && ParentIndex < static_cast<int32>(DesiredGlobalMatrices.size());
			const FMatrix CurrentLocalMatrix = bHasParent
				? CurrentGlobalMatrices[BoneIndex] * CurrentGlobalMatrices[ParentIndex].GetInverse()
				: CurrentGlobalMatrices[BoneIndex];

			DesiredGlobalMatrices[BoneIndex] = bHasParent
				? CurrentLocalMatrix * DesiredGlobalMatrices[ParentIndex]
				: CurrentLocalMatrix;
		}

		TArray<FTransform> DesiredLocalTransforms;
		DesiredLocalTransforms.resize(Asset->Bones.size());
		// SkinnedMeshComponent가 소비하는 값은 local pose이므로 parent global inverse를 곱한다.
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
			const FMatrix LocalMatrix = ParentIndex >= 0 && ParentIndex < static_cast<int32>(DesiredGlobalMatrices.size())
				? DesiredGlobalMatrices[BoneIndex] * DesiredGlobalMatrices[ParentIndex].GetInverse()
				: DesiredGlobalMatrices[BoneIndex];
			DesiredLocalTransforms[BoneIndex] = FTransform(LocalMatrix);
		}
		Comp->SetBoneLocalTransforms(DesiredLocalTransforms);
	}
}
