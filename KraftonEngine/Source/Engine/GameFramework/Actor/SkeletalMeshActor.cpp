#include "SkeletalMeshActor.h"
#include "Runtime/Engine.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Object/Object.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Animation/AnimationMode.h"
#include "Animation/Instance/CharacterAnimInstance.h"

namespace
{
	constexpr const char* YuiRagdollTestActorName = "ASkeletalMeshActor_0";
	constexpr const char* YuiRagdollTestMeshPath = "Content/Data/hirasawa-yui/IdleWithSkin_SkeletalMesh.uasset";
	constexpr const char* FurinaRagdollTestActorName = "ASkeletalMeshActor_1";
	constexpr const char* FurinaRagdollTestMeshPath = "Content/Data/FurinaFBX/Furina_SkeletalMesh.uasset";

	void AddSphereBody(UPhysicsAsset* PhysicsAsset, const char* BoneName, float Radius)
	{
		if (!PhysicsAsset) return;

		UBodySetup* BodySetup = UObjectManager::Get().CreateObject<UBodySetup>(PhysicsAsset);
		BodySetup->SetBoneName(FName(BoneName));

		FKSphereElem Sphere;
		Sphere.Name = FString(BoneName) + " Test Sphere";
		Sphere.Radius = Radius;
		BodySetup->GetAggGeom().SphereElems.push_back(Sphere);

		PhysicsAsset->BodySetups.push_back(BodySetup);
	}

	void AddPosePreservingConstraint(
		UPhysicsAsset* PhysicsAsset,
		USkeletalMeshComponent* MeshComponent,
		const char* ParentBoneName,
		const char* ChildBoneName)
	{
		if (!PhysicsAsset || !MeshComponent) return;

		FTransform ParentWorld;
		FTransform ChildWorld;
		if (!MeshComponent->GetBoneWorldTransformByName(ParentBoneName, ParentWorld)
			|| !MeshComponent->GetBoneWorldTransformByName(ChildBoneName, ChildWorld))
		{
			return;
		}

		FConstraintInstance Constraint;
		Constraint.ConstraintName = FString(ParentBoneName) + " -> " + ChildBoneName;
		Constraint.ParentBoneName = FName(ParentBoneName);
		Constraint.ChildBoneName = FName(ChildBoneName);
		// 임시 PIE 프리셋은 관절 연결 여부를 눈으로 확인하기 쉽도록 기본값보다 넓게 움직인다.
		Constraint.Option.TwistLimitDegrees = 60.f;
		Constraint.Option.Swing1LimitDegrees = 60.f;
		Constraint.Option.Swing2LimitDegrees = 60.f;

		// Joint 기준점을 자식 bone 원점으로 둔다.
		// 두 frame을 시작 pose에서 일치시키면 ragdoll을 켜는 순간 body가 갑자기 당겨지지 않는다.
		Constraint.ParentFrame = FTransform(ChildWorld.ToMatrix() * ParentWorld.ToMatrix().GetInverse());
		Constraint.ChildFrame = FTransform();
		PhysicsAsset->ConstraintSetups.push_back(Constraint);
	}

	bool PrepareEmptyPhysicsAsset(
		USkeletalMeshComponent* MeshComponent,
		const char* ExpectedMeshPath,
		UPhysicsAsset*& OutPhysicsAsset)
	{
		OutPhysicsAsset = nullptr;
		if (!MeshComponent || MeshComponent->GetSkeletalMeshPath() != ExpectedMeshPath) return false;

		USkeletalMesh* SkeletalMesh = MeshComponent->GetSkeletalMesh();
		OutPhysicsAsset = SkeletalMesh ? SkeletalMesh->EnsurePhysicsAsset() : nullptr;
		return OutPhysicsAsset && !OutPhysicsAsset->HasAnyBodySetup();
	}

	bool BuildYuiRagdollTestPreset(USkeletalMeshComponent* MeshComponent)
	{
		UPhysicsAsset* PhysicsAsset = nullptr;
		if (!PrepareEmptyPhysicsAsset(MeshComponent, YuiRagdollTestMeshPath, PhysicsAsset)) return false;

		// Editor가 없는 동안 PIE에서 생성/constraint/sync 흐름을 검증하기 위한 최소 프리셋.
		// Sphere만 사용해 capsule 축 보정과 세부 피팅 문제를 분리한다.
		AddSphereBody(PhysicsAsset, "Bip001 Pelvis", 0.14f);
		AddSphereBody(PhysicsAsset, "Bip001 Spine", 0.12f);
		AddSphereBody(PhysicsAsset, "Bip001 Spine1", 0.14f);
		AddSphereBody(PhysicsAsset, "Bip001 Neck", 0.08f);
		AddSphereBody(PhysicsAsset, "Bip001 Head", 0.14f);
		AddSphereBody(PhysicsAsset, "Bip001 L UpperArm", 0.08f);
		AddSphereBody(PhysicsAsset, "Bip001 L Forearm", 0.07f);
		AddSphereBody(PhysicsAsset, "Bip001 R UpperArm", 0.08f);
		AddSphereBody(PhysicsAsset, "Bip001 R Forearm", 0.07f);
		AddSphereBody(PhysicsAsset, "Bip001 L Thigh", 0.10f);
		AddSphereBody(PhysicsAsset, "Bip001 L Calf", 0.085f);
		AddSphereBody(PhysicsAsset, "Bip001 R Thigh", 0.10f);
		AddSphereBody(PhysicsAsset, "Bip001 R Calf", 0.085f);

		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 Pelvis", "Bip001 Spine");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 Spine", "Bip001 Spine1");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 Spine1", "Bip001 Neck");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 Neck", "Bip001 Head");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 Spine1", "Bip001 L UpperArm");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 L UpperArm", "Bip001 L Forearm");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 Spine1", "Bip001 R UpperArm");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 R UpperArm", "Bip001 R Forearm");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 Pelvis", "Bip001 L Thigh");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 L Thigh", "Bip001 L Calf");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 Pelvis", "Bip001 R Thigh");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Bip001 R Thigh", "Bip001 R Calf");

		return true;
	}

	bool BuildFurinaRagdollTestPreset(USkeletalMeshComponent* MeshComponent)
	{
		UPhysicsAsset* PhysicsAsset = nullptr;
		if (!PrepareEmptyPhysicsAsset(MeshComponent, FurinaRagdollTestMeshPath, PhysicsAsset)) return false;

		// Furina는 Yui와 skeleton 이름 규칙이 달라 별도 프리셋을 사용한다.
		// 첫 검증에서는 몸통, 팔, 다리의 핵심 bone만 연결한다.
		AddSphereBody(PhysicsAsset, "Hips", 0.14f);
		AddSphereBody(PhysicsAsset, "Spine", 0.12f);
		AddSphereBody(PhysicsAsset, "Chest", 0.14f);
		AddSphereBody(PhysicsAsset, "Upper Chest", 0.14f);
		AddSphereBody(PhysicsAsset, "Neck", 0.08f);
		AddSphereBody(PhysicsAsset, "Head", 0.14f);
		AddSphereBody(PhysicsAsset, "Left arm", 0.08f);
		AddSphereBody(PhysicsAsset, "Left elbow", 0.07f);
		AddSphereBody(PhysicsAsset, "Right arm", 0.08f);
		AddSphereBody(PhysicsAsset, "Right elbow", 0.07f);
		AddSphereBody(PhysicsAsset, "Left leg", 0.10f);
		AddSphereBody(PhysicsAsset, "Left knee", 0.085f);
		AddSphereBody(PhysicsAsset, "Right leg", 0.10f);
		AddSphereBody(PhysicsAsset, "Right knee", 0.085f);

		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Hips", "Spine");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Spine", "Chest");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Chest", "Upper Chest");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Upper Chest", "Neck");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Neck", "Head");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Upper Chest", "Left arm");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Left arm", "Left elbow");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Upper Chest", "Right arm");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Right arm", "Right elbow");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Hips", "Left leg");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Left leg", "Left knee");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Hips", "Right leg");
		AddPosePreservingConstraint(PhysicsAsset, MeshComponent, "Right leg", "Right knee");

		return true;
	}

	bool BuildRagdollTestPreset(const FString& ActorName, USkeletalMeshComponent* MeshComponent)
	{
		if (ActorName == YuiRagdollTestActorName)
		{
			return BuildYuiRagdollTestPreset(MeshComponent);
		}
		if (ActorName == FurinaRagdollTestActorName)
		{
			return BuildFurinaRagdollTestPreset(MeshComponent);
		}
		return false;
	}

	bool IsRagdollTestTarget(const FString& ActorName, USkeletalMeshComponent* MeshComponent)
	{
		if (!MeshComponent) return false;

		const FString& MeshPath = MeshComponent->GetSkeletalMeshPath();
		return (ActorName == YuiRagdollTestActorName && MeshPath == YuiRagdollTestMeshPath)
			|| (ActorName == FurinaRagdollTestActorName && MeshPath == FurinaRagdollTestMeshPath);
	}
}

void ASkeletalMeshActor::BeginPlay()
{
	Super::BeginPlay();

	SkeletalMeshComponent = GetComponentByClass<USkeletalMeshComponent>();
	const FString ActorName = GetName();
	if (IsRagdollTestTarget(ActorName, SkeletalMeshComponent))
	{
		// PIE를 다시 실행하면 캐시된 메시가 이전 테스트 프리셋을 유지할 수 있다.
		// 프리셋 생성 여부와 무관하게 테스트 액터는 매번 랙돌을 활성화한다.
		BuildRagdollTestPreset(ActorName, SkeletalMeshComponent);
		SkeletalMeshComponent->SetSimulateRagdoll(true);
		UE_LOG("[RagdollTest] %s: runtime bodies=%d, constraints=%d",
			GetName().c_str(),
			static_cast<int32>(SkeletalMeshComponent->GetBodies().size()),
			static_cast<int32>(SkeletalMeshComponent->GetConstraints().size()));
	}
}

void ASkeletalMeshActor::InitDefaultComponents(const FString& SkeletalMeshFileName)
{
	SkeletalMeshComponent = AddComponent<USkeletalMeshComponent>();
	SetRootComponent(SkeletalMeshComponent);

	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	USkeletalMesh* Asset = FMeshManager::LoadSkeletalMesh(SkeletalMeshFileName, Device);

	SkeletalMeshComponent->SetSkeletalMesh(Asset);

	// Phase 5 데모: 확장 FSM (UCharacterAnimInstance) 자동 wiring.
	// 순서 — Class 먼저 (Mode==None 이라 재초기화 미발생) → Mode=Custom 전환 시 InitializeAnimation 1회.
	SkeletalMeshComponent->SetAnimInstanceClass(UCharacterAnimInstance::StaticClass());
	SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationCustom);
}
