#include "PhysXPhysicsScene.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Math/MathUtils.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"

#include <PxPhysicsAPI.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
	constexpr float MatrixDecomposeTolerance = 1.0e-6f;
	constexpr float RagdollReanchorAngularMarginDegrees = 40.0f;
	constexpr float RagdollReanchorLinearTolerance = 5.0f;

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
			// Row-vector matrices compose local * world, so quaternion removal applies the component inverse first.
			(ComponentWorldRotationInv * BodyWorldRotation.GetNormalized()).GetNormalized(),
			FVector::OneVector);
	}

	float GetAngularLimitWithMarginRad(EAngularConstraintMotion Motion, float LimitDegrees)
	{
		switch (Motion)
		{
		case EAngularConstraintMotion::Free:
			return -1.0f;
		case EAngularConstraintMotion::Locked:
			return RagdollReanchorAngularMarginDegrees * FMath::DegToRad;
		case EAngularConstraintMotion::Limited:
		default:
			return (FMath::ClampMin(LimitDegrees, 0.0f) + RagdollReanchorAngularMarginDegrees) * FMath::DegToRad;
		}
	}

	bool IsAngleOutsideLimit(float AngleRad, EAngularConstraintMotion Motion, float LimitDegrees)
	{
		const float LimitRad = GetAngularLimitWithMarginRad(Motion, LimitDegrees);
		return LimitRad >= 0.0f && std::fabs(AngleRad) > LimitRad;
	}

	bool ShouldReanchorRagdollJoint(
		physx::PxD6Joint* Joint,
		physx::PxRigidActor* ParentActor,
		physx::PxRigidActor* ChildActor,
		const FConstraintOption& Option)
	{
		if (!Joint || !ParentActor || !ChildActor)
		{
			return false;
		}

		const physx::PxTransform ParentAnchorWorld =
			ParentActor->getGlobalPose() * Joint->getLocalPose(physx::PxJointActorIndex::eACTOR0);
		const physx::PxTransform ChildAnchorWorld =
			ChildActor->getGlobalPose() * Joint->getLocalPose(physx::PxJointActorIndex::eACTOR1);
		const physx::PxVec3 AnchorDelta = ParentAnchorWorld.p - ChildAnchorWorld.p;
		if (AnchorDelta.magnitudeSquared() > RagdollReanchorLinearTolerance * RagdollReanchorLinearTolerance)
		{
			return true;
		}

		return IsAngleOutsideLimit(Joint->getTwistAngle(), Option.TwistMotion, Option.TwistLimitDegrees)
			|| IsAngleOutsideLimit(Joint->getSwingYAngle(), Option.Swing1Motion, Option.Swing1LimitDegrees)
			|| IsAngleOutsideLimit(Joint->getSwingZAngle(), Option.Swing2Motion, Option.Swing2LimitDegrees);
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

	// Ragdoll을 켜는 순간 body는 현재 animation 포즈로 teleport된다. 이 포즈가 authored joint limit이나
	// locked linear anchor를 크게 위반하면 solver가 첫 프레임에 큰 보정 속도를 만들 수 있다.
	// 다만 모든 joint를 현재 포즈 기준으로 re-anchor하면 점프처럼 접힌 자세가 neutral이 되어 ragdoll이
	// 굳어 보인다. 그래서 authored constraint가 감당 가능한 joint는 그대로 두고, 위험한 위반만 re-anchor한다.
	int32 ReanchoredCount = 0;
	int32 PreservedCount = 0;
	for (auto& Constraint : Comp->GetConstraints())
	{
		if (!Constraint || !Constraint->IsValidConstraint()) continue;

		physx::PxJoint* Joint = Constraint->GetJointHandle();
		physx::PxD6Joint* D6Joint = Joint ? Joint->is<physx::PxD6Joint>() : nullptr;
		physx::PxRigidActor* ParentActor = Constraint->ParentBody
			? Constraint->ParentBody->GetPxRigidActor()
			: nullptr;
		physx::PxRigidActor* ChildActor = Constraint->ChildBody
			? Constraint->ChildBody->GetPxRigidActor()
			: nullptr;
		if (!Joint || !D6Joint || !ParentActor || !ChildActor) continue;

		if (!ShouldReanchorRagdollJoint(D6Joint, ParentActor, ChildActor, Constraint->Setup.Option))
		{
			++PreservedCount;
			continue;
		}

		const physx::PxTransform ChildLocalPose = Joint->getLocalPose(physx::PxJointActorIndex::eACTOR1);
		const physx::PxTransform ParentWorldPose = ParentActor->getGlobalPose();
		const physx::PxTransform ChildWorldPose = ChildActor->getGlobalPose();

		// ca2w: ConstraintFrameA to World
		// cb2w: ConstraintFrameB to World
		// cA2w == cB2w 가 되도록 ParentLocal 재설정 → 상대 변환(위치+회전) = identity = 위반 0.
		// cB2w = ChildWorldPose * ChildLocalPose,  ParentLocal = ParentWorldPose^-1 * cB2w
		const physx::PxTransform ChildJointWorld = ChildWorldPose * ChildLocalPose;
		const physx::PxTransform DesiredParentLocal = ParentWorldPose.getInverse() * ChildJointWorld;
		Joint->setLocalPose(physx::PxJointActorIndex::eACTOR0, DesiredParentLocal);
		++ReanchoredCount;
	}

	if (ReanchoredCount > 0)
	{
		UE_LOG("[PhysX] Ragdoll conditionally re-anchored joints to current pose (pos+rot): count=%d preserved=%d", ReanchoredCount, PreservedCount);
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

void FPhysXPhysicsScene::SyncKinematicPhysicsAssetBodiesToBones()
{
	// ragdoll이 꺼진 동안에는 PhysicsAsset body가 전부 kinematic이다(SetSimulatePhysics(false)).
	// kinematic body는 "코드가 매 frame target을 줘야" 움직이는데, 그 경로가 없으면
	// BeginPlay에서 생성된 위치(첫 pose)에 그대로 멈춰 animation/캐릭터 이동을 따라오지 않는다.
	// → idle 캐릭터를 raycast/overlap으로 부위 타격하려 해도 collider가 화면과 어긋나 헛맞는다.
	// 여기서 매 frame bone world pose를 kinematic target으로 먹여 그 누락된 절반을 채운다.
	// (ragdoll ON 컴포넌트는 post-simulate의 SyncPhysicsAssetBodiesToBones가 body→bone 반대 방향 담당.)
	for (USkeletalMeshComponent* Comp : SkeletalPhysicsComponents)
	{
		if (!Comp || Comp->IsRagdollSimulating()) continue;

		for (auto& Body : Comp->GetBodies())
		{
			if (!Body || !Body->IsValidBodyInstance()) continue;

			// 부분 타격(hit reaction)으로 일부 body만 dynamic으로 전환된 경우 그 body는 물리가 구동하므로 건너뛴다.
			// (dynamic body에 setKinematicTarget은 PhysX가 무시하지만, 의도를 코드로 명시하고 불필요한 계산도 피한다.)
			if (!Body->IsKinematic()) continue;

			FTransform BoneWorldTransform;
			if (!Comp->GetBoneWorldTransformByIndex(Body->GetBoneIndex(), BoneWorldTransform)) continue;

			Body->SetKinematicTarget(BoneWorldTransform.Location, BoneWorldTransform.Rotation);
		}
	}
}

void FPhysXPhysicsScene::SyncPhysicsAssetBodiesToBones(float DeltaTime)
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
		// 여기까지는 "이번 프레임의 순수 physics pose"를 skeleton local pose로 만든 상태다.
		// Animation -> Ragdoll 전환 중이면 컴포넌트가 캡처해둔 animation pose와 이 배열을 섞고,
		// 전환 중이 아니면 배열을 그대로 둔다. PhysX scene은 blend 상태를 직접 소유하지 않는다.
		Comp->ApplyRagdollBlendToPhysics(DeltaTime, DesiredLocalTransforms);
		Comp->SetBoneLocalTransforms(DesiredLocalTransforms);
	}
}
