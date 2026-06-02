#pragma once

#include "Component/Primitive/SkinnedMeshComponent.h"
#include "Animation/AnimationMode.h"
#include "Object/Ptr/SubclassOf.h"
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"

#include <memory>

#include "Source/Engine/Component/Primitive/SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSingleNodeInstance;
class UAnimSequenceBase;
class UClass;
struct FPoseContext;

// SkeletalMesh 전용 render proxy만 제공하는 얇은 wrapper.
// Skinning/bone/material/bounds 상태는 모두 USkinnedMeshComponent가 소유한다.
UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	GENERATED_BODY()
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override;

    // Render access 섹션: SceneProxy
    FPrimitiveSceneProxy* CreateSceneProxy() override;

    // Mesh 가 바뀌면 AnimInstance 도 새 SkeletalMesh 기준으로 재구성해야 하므로 override.
    void SetSkeletalMesh(USkeletalMesh* InMesh) override;

    // SingleNode 재생 편의 API.
    void PlayAnimation(UAnimSequenceBase* NewAnimToPlay, bool bLooping);
    void StopAnimation();

    // Animation 섹션: Mode 에 따라 AnimInstance 의 생성/파기를 컴포넌트가 책임진다.
    //   - None              : AnimInstance 미생성. BoneEdit 만 적용.
    //   - AnimationSingleNode: UAnimSingleNodeInstance 자동 생성, AnimationData 로 구동.
    //   - AnimationCustom   : AnimInstanceClass 가 가리키는 자식 클래스를 FObjectFactory 로 인스턴스화.
    void SetAnimationMode(EAnimationMode InMode);
    EAnimationMode GetAnimationMode() const { return AnimationMode; }

    // SingleNode 모드용 헬퍼. Custom 모드에선 무시 (자체 인스턴스가 자체 시퀀스를 관리).
    void SetAnimation(UAnimSequenceBase* InAsset);
    bool CanUseAnimation(UAnimSequenceBase* InAsset) const;
    UAnimSequenceBase* GetAnimation() const { return AnimationData.AnimToPlay.Get(); }
    void SetPlayRate(float InRate);
    void SetLooping(bool bInLoop);
    void SetPlaying(bool bInPlay);
    const FSingleAnimationPlayData& GetAnimationData() const { return AnimationData; }

    // Custom 모드용. 클래스 변경 시 다음 InitializeAnimation 에서 재인스턴스화.
    // 슬롯은 TSubclassOf<UAnimInstance> — 잘못된 클래스 대입은 nullptr 로 흡수.
    void SetAnimInstanceClass(UClass* InClass);
    UClass* GetAnimInstanceClass() const { return AnimInstanceClass.Get(); }

    // 외부에서 직접 만든 인스턴스 주입 (테스트 / 특수 케이스). Mode 와 무관하게 즉시 교체.
    void SetAnimInstance(UAnimInstance* InInstance);
    UAnimInstance* GetAnimInstance() const { return AnimInstance; }

    // SingleNode 모드에서 현재 자동 생성된 노드를 반환한다. NodeName 은 현재 단일 노드 구조에서는 무시한다.
    UAnimSingleNodeInstance* GetAnimNodeInstance(FName NodeName) const;

    // Mode/Class/SkeletalMesh 변경 후 일관성 재정렬. SetSkeletalMesh override 안에서 자동 호출됨.
    void InitializeAnimation();
    void ClearAnimInstance();

    // Editor / 직렬화 통합.
    void GetEditableProperties(TArray<FPropertyValue>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;

    // true: 현재 animation pose를 PhysX body 시작 위치로 복사한 뒤 ragdoll simulation을 시작한다.
    // false: simulation을 끄고 현재 ragdoll pose에서 animation pose로 짧게 blend-out한다.
    void SetSimulateRagdoll(bool bEnable);
    bool IsRagdollSimulating() const { return bRagdollSimulating; }
    // PhysX write-back이 만든 최종 local pose를 렌더 포즈에 적용하기 직전에 호출된다.
    // 컴포넌트가 전환 시작 포즈와 시간을 소유하므로 PhysX 레이어는 blend 상태를 알 필요가 없다.
    void ApplyRagdollBlendToPhysics(float DeltaTime, TArray<FTransform>& InOutPhysicsLocalPose);

    void CachePhysicsAssetRuntimeScale();
    void InvalidatePhysicsAssetRuntimeScale();

    TArray<std::unique_ptr<FBodyInstance>>& GetBodies() { return Bodies; }
    const TArray<std::unique_ptr<FBodyInstance>>& GetBodies() const { return Bodies; }
    TArray<std::unique_ptr<FConstraintInstance>>& GetConstraints() { return Constraints; }
    const TArray<std::unique_ptr<FConstraintInstance>>& GetConstraints() const { return Constraints; }

protected:
    void BeginPlay() override;
    void EndPlay() override;
    void OnTransformDirty() override;

    // 매 프레임 AnimInstance 평가 → 결과 포즈를 SetBoneLocalTransforms 로 푸시.
    // 이 경로가 CPU skinning 과 bounds dirty 를 한 번에 처리한다.
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;

    bool EvaluateAnimInstance(float DeltaTime);

private:
    void LoadAnimationFromPath();
    void RecreatePhysicsAssetBodiesIfScaleChanged();
    void CaptureRagdollVelocityPose(float DeltaTime);
    void ApplyCachedRagdollVelocitiesToBodies();
    // Animation -> Ragdoll 전환 순간의 현재 local pose를 저장한다.
    // 물리는 즉시 dynamic으로 돌지만, 렌더 포즈는 이 pose에서 physics pose로 짧게 따라간다.
    void BeginRagdollBlendToPhysics();
    void ResetRagdollBlendToPhysics();
    // Ragdoll -> Animation 전환 순간의 현재 physics local pose를 저장한다.
    // 물리는 꺼지지만, 렌더 포즈는 이 pose에서 새 animation pose로 짧게 따라간다.
    void BeginRagdollBlendToAnimation();
    void ResetRagdollBlendToAnimation();
    bool TickRagdollBlendToAnimation(float DeltaTime);
    bool EvaluateAnimInstancePose(float DeltaTime, FPoseContext& OutPose);

protected:
    // Animation 런타임 상태.
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Animation Mode", Enum=EAnimationMode)
    EAnimationMode             AnimationMode = EAnimationMode::None;
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Animation Data", Type=Struct)
    FSingleAnimationPlayData   AnimationData;
    UPROPERTY(Edit, Save, Category="Animation", DisplayName="Anim Instance Class", Type=ClassRef, AllowedClass=UAnimInstance)
    TSubclassOf<UAnimInstance> AnimInstanceClass;
    UPROPERTY(Save, Instanced, Category="Animation", DisplayName="Anim Instance", Type=ObjectRef, AllowedClass=UAnimInstance)
    UAnimInstance*             AnimInstance  = nullptr;

    // PhysicsAsset instantiate 단계에서 채워지는 runtime 상태. 에셋에 저장하지 않는다.
    // Bodies는 BodySetup과 bone에, Constraints는 두 runtime body 사이의 PxJoint에 대응한다.
    // 둘 다 이 컴포넌트가 소유한다(unique_ptr). PhysicsScene은 시뮬레이션만 하고 소유하지 않는다.
    TArray<std::unique_ptr<FBodyInstance>>       Bodies;
    TArray<std::unique_ptr<FConstraintInstance>> Constraints;

	UPROPERTY(Edit, Save, Category = "Physics", DisplayName = "Ragdoll Simulating")
    bool bRagdollSimulating = false;
    // 랙돌이 켜진 뒤 렌더링 본 포즈가 물리 포즈로 완전히 넘어가는 시간.
    // 0 이하이면 기존처럼 즉시 physics pose를 쓴다.
    UPROPERTY(Edit, Save, Category = "Physics", DisplayName = "Ragdoll Blend To Physics Time")
    float RagdollBlendToPhysicsDuration = 0.20f;
    // 랙돌이 꺼진 뒤 렌더링 본 포즈가 animation pose로 완전히 넘어가는 시간.
    // 0 이하이면 기존처럼 즉시 animation pose를 쓴다.
    UPROPERTY(Edit, Save, Category = "Physics", DisplayName = "Ragdoll Blend To Animation Time")
    float RagdollBlendToAnimationDuration = 0.20f;

    // 전환 시작 시점의 skeleton local pose. PhysX body는 이미 animation pose로 동기화되어 있으므로
    // 여기서는 visual pop을 줄이기 위한 렌더 포즈 blend 기준으로만 사용한다.
    TArray<FTransform> RagdollBlendStartLocalPose;
    float RagdollBlendToPhysicsElapsed = 0.0f;
    bool bRagdollBlendToPhysicsActive = false;
    // 랙돌을 끄는 순간 화면에 보이던 physics-driven local pose.
    // 첫 복귀 버전에서는 root/pelvis 정렬 없이 이 pose에서 현재 animation pose로만 보간한다.
    TArray<FTransform> RagdollBlendToAnimationStartLocalPose;
    float RagdollBlendToAnimationElapsed = 0.0f;
    bool bRagdollBlendToAnimationActive = false;

    TArray<FTransform> PreviousRagdollVelocityWorldPose;
    TArray<FTransform> CurrentRagdollVelocityWorldPose;
    float RagdollVelocitySampleDeltaTime = 0.0f;
    bool bHasRagdollVelocitySample = false;

    FVector CachedPhysicsAssetRuntimeScale = FVector::OneVector;
    bool bHasCachedPhysicsAssetRuntimeScale = false;
    bool bRecreatingPhysicsAssetForScaleChange = false;
};
