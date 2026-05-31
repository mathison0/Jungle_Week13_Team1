#include "PhysXPhysicsScene.h"

#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"

#include <algorithm>

bool FPhysXPhysicsScene::InstantiatePhysicsAssetBodies(USkeletalMeshComponent* Comp)
{
	if (!Comp || !Scene || !Physics || !DefaultMaterial) return false;

	DestroyPhysicsAssetBodies(Comp);

	USkeletalMesh* SkeletalMesh = Comp->GetSkeletalMesh();
	UPhysicsAsset* PhysicsAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
	if (!PhysicsAsset || !PhysicsAsset->HasAnyBodySetup()) return false;

	TArray<FBodyInstance*>& RuntimeBodies = Comp->GetBodies();
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

		FBodyInstance* RuntimeBody = CreateBodyFromBodySetup(Comp, BodySetup, BoneWorldTransform, true);
		if (!RuntimeBody) continue;

		RuntimeBody->SetBodyIndex(BodySetupIndex);
		RuntimeBody->SetBoneIndex(BoneIndex);
		RuntimeBody->SetSimulatePhysics(Comp->GetSimulatePhysics());
		RuntimeBodies.push_back(RuntimeBody);
	}

	for (const FConstraintInstance& ConstraintSetup : PhysicsAsset->ConstraintSetups)
	{
		FBodyInstance* ParentBody = nullptr;
		FBodyInstance* ChildBody = nullptr;
		const int32 ParentIndex = PhysicsAsset->FindBodySetupIndexByBoneName(ConstraintSetup.ParentBoneName);
		const int32 ChildIndex = PhysicsAsset->FindBodySetupIndexByBoneName(ConstraintSetup.ChildBoneName);

		for (FBodyInstance* Body : RuntimeBodies)
		{
			if (!Body) continue;
			if (Body->GetBodyIndex() == ParentIndex) ParentBody = Body;
			if (Body->GetBodyIndex() == ChildIndex) ChildBody = Body;
		}

		if (!ParentBody || !ChildBody) continue;

		if (FConstraintInstance* Constraint = CreateConstraint(
			ParentBody, ChildBody, ConstraintSetup.Option,
			ConstraintSetup.ParentFrame, ConstraintSetup.ChildFrame, ConstraintSetup.ConstraintName))
		{
			Comp->GetConstraints().push_back(Constraint);
		}
	}

	if (!RuntimeBodies.empty())
	{
		SkeletalPhysicsComponents.push_back(Comp);
		return true;
	}
	return false;
}

void FPhysXPhysicsScene::DestroyPhysicsAssetBodies(USkeletalMeshComponent* Comp)
{
	if (!Comp) return;

	for (FConstraintInstance* Constraint : Comp->GetConstraints())
	{
		DestroyConstraint(Constraint);
	}
	Comp->GetConstraints().clear();

	TArray<FBodyInstance*> Bodies = Comp->GetBodies();
	Comp->GetBodies().clear();
	for (FBodyInstance* Body : Bodies)
	{
		DestroyBody(Body);
	}

	SkeletalPhysicsComponents.erase(
		std::remove(SkeletalPhysicsComponents.begin(), SkeletalPhysicsComponents.end(), Comp),
		SkeletalPhysicsComponents.end());
}

bool FPhysXPhysicsScene::SyncPhysicsAssetBodiesToComponentPose(USkeletalMeshComponent* Comp, bool bResetVelocity)
{
	if (!Comp) return false;

	bool bSynced = false;
	for (FBodyInstance* Body : Comp->GetBodies())
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

	for (FBodyInstance* Body : Comp->GetBodies())
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

		TArray<FMatrix> DesiredGlobalMatrices;
		Comp->GetCurrentBoneGlobalMatrices(DesiredGlobalMatrices);
		if (DesiredGlobalMatrices.size() != Asset->Bones.size()) continue;

		const FMatrix& ComponentWorldInv = Comp->GetWorldInverseMatrix();
		for (FBodyInstance* Body : Comp->GetBodies())
		{
			if (!Body || !Body->IsValidBodyInstance()) continue;

			const int32 BoneIndex = Body->GetBoneIndex();
			if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size())) continue;

			const FTransform BodyWorldTransform(
				Body->GetEngineWorldLocation(),
				Body->GetEngineWorldRotation(),
				FVector::OneVector);
			DesiredGlobalMatrices[BoneIndex] = BodyWorldTransform.ToMatrix() * ComponentWorldInv;
		}

		TArray<FTransform> DesiredLocalTransforms;
		DesiredLocalTransforms.resize(Asset->Bones.size());
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
