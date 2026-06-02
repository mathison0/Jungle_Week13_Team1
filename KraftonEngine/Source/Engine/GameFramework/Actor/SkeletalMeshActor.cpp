#include "SkeletalMeshActor.h"

#include "Animation/AnimationMode.h"
#include "Animation/Instance/CharacterAnimInstance.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Core/Logging/Log.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Physics/PhysicsAsset.h"
#include "Runtime/Engine.h"

static constexpr const char* YuiRagdollTestActorName = "ASkeletalMeshActor_0";
static constexpr const char* YuiRagdollTestMeshPath = "Content/Data/hirasawa-yui/IdleWithSkin_SkeletalMesh.uasset";
static constexpr const char* FurinaRagdollTestActorName = "ASkeletalMeshActor_1";
static constexpr const char* FurinaRagdollTestMeshPath = "Content/Data/FurinaFBX/Furina_SkeletalMesh.uasset";

static bool IsRagdollTestTarget(const FString& ActorName, USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent) return false;

	const FString& MeshPath = MeshComponent->GetSkeletalMeshPath();
	return (ActorName == YuiRagdollTestActorName && MeshPath == YuiRagdollTestMeshPath)
		|| (ActorName == FurinaRagdollTestActorName && MeshPath == FurinaRagdollTestMeshPath);
}

void ASkeletalMeshActor::BeginPlay()
{
	Super::BeginPlay();

	SkeletalMeshComponent = GetComponentByClass<USkeletalMeshComponent>();
	const FString ActorName = GetName();
	if (IsRagdollTestTarget(ActorName, SkeletalMeshComponent))
	{
		UPhysicsAsset* PhysicsAsset = nullptr;
		if (USkeletalMesh* SkeletalMesh = SkeletalMeshComponent->GetSkeletalMesh())
		{
			PhysicsAsset = SkeletalMesh->GetPhysicsAsset();
		}

		UE_LOG("[RagdollTest] %s: imported physics asset bodies=%d constraints=%d",
			GetName().c_str(),
			PhysicsAsset ? static_cast<int32>(PhysicsAsset->BodySetups.size()) : 0,
			PhysicsAsset ? static_cast<int32>(PhysicsAsset->ConstraintSetups.size()) : 0);

		SkeletalMeshComponent->SetSimulateRagdoll(true);
		UE_LOG("[RagdollTest] %s: runtime bodies=%d constraints=%d",
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
