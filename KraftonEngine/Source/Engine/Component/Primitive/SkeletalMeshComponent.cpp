#include "SkeletalMeshComponent.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"

#include "Animation/AnimationManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Sequence/AnimSequenceBase.h"
#include "Animation/Instance/AnimSingleNodeInstance.h"
#include "Animation/PoseContext.h"
#include "Asset/AssetRegistry.h"
#include "Core/Logging/Log.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Object/Object.h"
#include "Object/Reflection/ObjectFactory.h"
#include "Object/Reflection/UClass.h"
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/IPhysicsScene.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace
{
    constexpr float MinRagdollVelocitySampleDeltaTime = 1.0e-4f;
    constexpr float MaxRagdollInitialLinearSpeed = 500.0f;
    constexpr float MaxRagdollInitialAngularSpeed = 500.0f;

    bool IsScaleNearlyEqual(const FVector& A, const FVector& B, float Tolerance = 1.0e-4f)
    {
        return std::fabs(A.X - B.X) <= Tolerance
            && std::fabs(A.Y - B.Y) <= Tolerance
            && std::fabs(A.Z - B.Z) <= Tolerance;
    }

    float SmoothStep01(float Alpha)
    {
        Alpha = std::clamp(Alpha, 0.0f, 1.0f);
        // 전환 시작/끝에서 기울기를 0으로 만들어 첫 프레임과 마지막 프레임의 튐을 줄인다.
        return Alpha * Alpha * (3.0f - 2.0f * Alpha);
    }

    FTransform BlendLocalTransform(const FTransform& A, const FTransform& B, float Alpha)
    {
        FTransform Result;
        Result.Location = A.Location + (B.Location - A.Location) * Alpha;
        // Euler 보간은 축 순서와 wrapping 때문에 쉽게 꼬이므로 rotation은 quaternion slerp로 처리한다.
        Result.Rotation = FQuat::Slerp(A.Rotation.GetNormalized(), B.Rotation.GetNormalized(), Alpha).GetNormalized();
        Result.Scale = A.Scale + (B.Scale - A.Scale) * Alpha;
        return Result;
    }

    FVector ClampVectorLength(const FVector& Vector, float MaxLength)
    {
        const float LengthSq = Vector.Dot(Vector);
        if (LengthSq <= MaxLength * MaxLength)
        {
            return Vector;
        }

        const float Length = std::sqrt(LengthSq);
        if (Length <= 1.0e-6f)
        {
            return FVector::ZeroVector;
        }

        return Vector * (MaxLength / Length);
    }

    FVector ComputeAngularVelocity(const FQuat& PreviousRotation, const FQuat& CurrentRotation, float DeltaTime)
    {
        // 프레임 간 회전 차이를 PhysX angular velocity로 변환한다.
        // 부호 반전은 Slerp와 같은 규칙으로 quaternion 최단 회전 경로를 고른다.
        FQuat Delta = (CurrentRotation.GetNormalized() * PreviousRotation.GetNormalized().Inverse()).GetNormalized();
        if (Delta.W < 0.0f)
        {
            Delta.X = -Delta.X;
            Delta.Y = -Delta.Y;
            Delta.Z = -Delta.Z;
            Delta.W = -Delta.W;
        }

        const float W = std::clamp(Delta.W, -1.0f, 1.0f);
        const float Angle = 2.0f * std::acos(W);
        const float SinHalf = std::sqrt((std::max)(0.0f, 1.0f - W * W));
        if (Angle <= 1.0e-5f || SinHalf <= 1.0e-5f)
        {
            return FVector::ZeroVector;
        }

        const FVector Axis(Delta.X / SinHalf, Delta.Y / SinHalf, Delta.Z / SinHalf);
        return ClampVectorLength(Axis * (Angle / DeltaTime), MaxRagdollInitialAngularSpeed);
    }
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    // EndPlay 없이 파괴되는 경로 대비 — World/Scene이 살아있으면 ragdoll body의 PhysX 자원을
    // 먼저 해제한 뒤 unique_ptr Bodies가 소멸하게 한다. (안 하면 PxActor 미해제 상태로 객체가 delete됨)
    if (Owner)
    {
        if (UWorld* World = Owner->GetWorld())
        {
            if (IPhysicsScene* PhysicsScene = World->GetPhysicsScene())
            {
                PhysicsScene->DestroyPhysicsAssetBodies(this);
            }
        }
    }
    ClearAnimInstance();
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
    return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::BeginPlay()
{
    Super::BeginPlay();

    if (Owner)
    {
        if (UWorld* World = Owner->GetWorld())
        {
            if (IPhysicsScene* PhysicsScene = World->GetPhysicsScene())
            {
                PhysicsScene->InstantiatePhysicsAssetBodies(this);
            }
        }
    }
}

void USkeletalMeshComponent::EndPlay()
{
    if (Owner)
    {
        if (UWorld* World = Owner->GetWorld())
        {
            if (IPhysicsScene* PhysicsScene = World->GetPhysicsScene())
            {
                PhysicsScene->DestroyPhysicsAssetBodies(this);
            }
        }
    }
    bRagdollSimulating = false;
    ResetRagdollBlendToPhysics();
    ResetRagdollBlendToAnimation();
    PreviousRagdollVelocityWorldPose.clear();
    CurrentRagdollVelocityWorldPose.clear();
    RagdollVelocitySampleDeltaTime = 0.0f;
    bHasRagdollVelocitySample = false;

    Super::EndPlay();
}

void USkeletalMeshComponent::OnTransformDirty()
{
    Super::OnTransformDirty();
    RecreatePhysicsAssetBodiesIfScaleChanged();
}

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InMesh)
{
    Super::SetSkeletalMesh(InMesh);
    ResetRagdollBlendToPhysics();
    ResetRagdollBlendToAnimation();
    // Mesh 가 바뀌면 이전 AnimInstance 가 가리키던 본 인덱스/카운트가 무의미해진다.
    // 새 SkeletalMesh 기준으로 AnimInstance 를 재인스턴스화한다.
    PreviousRagdollVelocityWorldPose.clear();
    CurrentRagdollVelocityWorldPose.clear();
    RagdollVelocitySampleDeltaTime = 0.0f;
    bHasRagdollVelocitySample = false;
    InitializeAnimation();
}

void USkeletalMeshComponent::CachePhysicsAssetRuntimeScale()
{
    CachedPhysicsAssetRuntimeScale = GetWorldScale();
    bHasCachedPhysicsAssetRuntimeScale = true;
}

void USkeletalMeshComponent::InvalidatePhysicsAssetRuntimeScale()
{
    bHasCachedPhysicsAssetRuntimeScale = false;
}

void USkeletalMeshComponent::RecreatePhysicsAssetBodiesIfScaleChanged()
{
    if (bRecreatingPhysicsAssetForScaleChange || Bodies.empty() || !bHasCachedPhysicsAssetRuntimeScale)
    {
        return;
    }

    const FVector CurrentScale = GetWorldScale();
    if (IsScaleNearlyEqual(CurrentScale, CachedPhysicsAssetRuntimeScale))
    {
        return;
    }

    if (!Owner)
    {
        InvalidatePhysicsAssetRuntimeScale();
        return;
    }

    UWorld* World = Owner->GetWorld();
    IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
    if (!PhysicsScene)
    {
        InvalidatePhysicsAssetRuntimeScale();
        return;
    }

    const bool bWasRagdollSimulating = bRagdollSimulating;
    ResetRagdollBlendToPhysics();
    ResetRagdollBlendToAnimation();
    bRecreatingPhysicsAssetForScaleChange = true;

    PhysicsScene->DestroyPhysicsAssetBodies(this);
    PhysicsScene->InstantiatePhysicsAssetBodies(this);

    if (!Bodies.empty())
    {
        PhysicsScene->SyncPhysicsAssetBodiesToComponentPose(this, true);
        PhysicsScene->SetPhysicsAssetBodiesSimulate(this, bWasRagdollSimulating);
    }

    bRagdollSimulating = bWasRagdollSimulating && !Bodies.empty();
    bRecreatingPhysicsAssetForScaleChange = false;
}

void USkeletalMeshComponent::CaptureRagdollVelocityPose(float DeltaTime)
{
    // 랙돌 진입 속도 샘플은 animation이 만든 pose일 때만 의미가 있다.
    // 랙돌이 켜진 뒤의 bone transform은 physics write-back 결과라 다시 속도 샘플로 쓰면 피드백이 생긴다.
    if (bRagdollSimulating)
    {
        return;
    }

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!Asset || Asset->Bones.empty())
    {
        PreviousRagdollVelocityWorldPose.clear();
        CurrentRagdollVelocityWorldPose.clear();
        RagdollVelocitySampleDeltaTime = 0.0f;
        bHasRagdollVelocitySample = false;
        return;
    }

    TArray<FTransform> NewWorldPose;
    NewWorldPose.resize(Asset->Bones.size());
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
    {
        FTransform BoneWorldTransform;
        if (!GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform))
        {
            PreviousRagdollVelocityWorldPose.clear();
            CurrentRagdollVelocityWorldPose.clear();
            RagdollVelocitySampleDeltaTime = 0.0f;
            bHasRagdollVelocitySample = false;
            return;
        }
        NewWorldPose[BoneIndex] = BoneWorldTransform;
    }

    // 본별 linear/angular velocity를 복원할 수 있도록 world pose 샘플을 직전/현재 2개만 유지한다.
    PreviousRagdollVelocityWorldPose = std::move(CurrentRagdollVelocityWorldPose);
    CurrentRagdollVelocityWorldPose = std::move(NewWorldPose);
    RagdollVelocitySampleDeltaTime = (std::isfinite(DeltaTime) && DeltaTime > 0.0f) ? DeltaTime : 0.0f;
    bHasRagdollVelocitySample =
        PreviousRagdollVelocityWorldPose.size() == CurrentRagdollVelocityWorldPose.size() &&
        RagdollVelocitySampleDeltaTime >= MinRagdollVelocitySampleDeltaTime;
}

void USkeletalMeshComponent::ApplyCachedRagdollVelocitiesToBodies()
{
    for (auto& Body : Bodies)
    {
        if (!Body || !Body->IsValidBodyInstance()) continue;

        const int32 BoneIndex = Body->GetBoneIndex();
        const bool bCanApplyBoneVelocity =
            bHasRagdollVelocitySample &&
            BoneIndex >= 0 &&
            BoneIndex < static_cast<int32>(PreviousRagdollVelocityWorldPose.size()) &&
            BoneIndex < static_cast<int32>(CurrentRagdollVelocityWorldPose.size());
        if (!bCanApplyBoneVelocity)
        {
            // animation 샘플이 없거나 무효이면 이전 PhysX velocity가 남지 않도록 명시적으로 초기화한다.
            Body->SetLinearVelocity(FVector::ZeroVector);
            Body->SetAngularVelocity(FVector::ZeroVector);
            continue;
        }

        const FTransform& Previous = PreviousRagdollVelocityWorldPose[BoneIndex];
        const FTransform& Current = CurrentRagdollVelocityWorldPose[BoneIndex];
        // world-space 본 이동량에는 actor 이동/root motion과 팔/다리 animation 관성이 함께 들어간다.
        const FVector LinearVelocity = ClampVectorLength(
            (Current.Location - Previous.Location) / RagdollVelocitySampleDeltaTime,
            MaxRagdollInitialLinearSpeed);
        const FVector AngularVelocity = ComputeAngularVelocity(
            Previous.Rotation,
            Current.Rotation,
            RagdollVelocitySampleDeltaTime);

        Body->SetLinearVelocity(LinearVelocity);
        Body->SetAngularVelocity(AngularVelocity);
    }
}

void USkeletalMeshComponent::PlayAnimation(UAnimSequenceBase* NewAnimToPlay, bool bLooping)
{
    SetAnimationMode(EAnimationMode::AnimationSingleNode);
    SetAnimation(NewAnimToPlay);
    SetLooping(bLooping);
    SetPlaying(NewAnimToPlay != nullptr);
}

void USkeletalMeshComponent::StopAnimation()
{
    SetAnimation(nullptr);
    SetPlaying(false);

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetCurrentTime(0.0f);
    }
}

// ──────────────────────────────────────────────
// Animation API
// ──────────────────────────────────────────────
void USkeletalMeshComponent::SetSimulateRagdoll(bool bEnable)
{
    if (bRagdollSimulating == bEnable)
    {
        return;
    }

    if (!Owner)
    {
        bRagdollSimulating = bEnable;
        if (!bEnable)
        {
            ResetRagdollBlendToPhysics();
        }
        return;
    }

    UWorld* World = Owner->GetWorld();
    IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
    if (!PhysicsScene)
    {
        bRagdollSimulating = bEnable;
        if (!bEnable)
        {
            ResetRagdollBlendToPhysics();
        }
        return;
    }

    if (bEnable)
    {
        if (Bodies.empty())
        {
            PhysicsScene->InstantiatePhysicsAssetBodies(this);
        }

        // 전환 순서가 중요하다.
        // 1) 현재 렌더링 중인 animation local pose를 blend 시작점으로 캡처한다.
        // 2) PhysX body를 같은 animation pose의 world transform으로 텔레포트한다.
        // 3) body를 dynamic으로 전환한다.
        //
        // 이 순서가 깨지면 첫 physics write-back에서 이전 body pose 또는 bind pose가 노출되어
        // blend를 넣어도 캐릭터가 한 프레임 튀어 보일 수 있다.
        BeginRagdollBlendToPhysics();
        ResetRagdollBlendToAnimation();
        PhysicsScene->SyncPhysicsAssetBodiesToComponentPose(this, false);
        PhysicsScene->SetPhysicsAssetBodiesSimulate(this, true);
        // PhysX는 kinematic actor에 velocity 쓰기를 거부하므로 body를 dynamic으로 바꾼 뒤 주입한다.
        ApplyCachedRagdollVelocitiesToBodies();
        bRagdollSimulating = !Bodies.empty();
        if (!bRagdollSimulating)
        {
            ResetRagdollBlendToPhysics();
        }
        return;
    }

    // 현재 BoneEdit pose는 마지막 PhysX write-back 결과다.
    // 물리를 끄기 전에 이 pose를 저장해야 화면에 보이는 랙돌 자세에서 animation pose로 이어진다.
    BeginRagdollBlendToAnimation();
    PhysicsScene->SetPhysicsAssetBodiesSimulate(this, false);
    bRagdollSimulating = false;
    ResetRagdollBlendToPhysics();
}

void USkeletalMeshComponent::BeginRagdollBlendToPhysics()
{
    // 재진입 또는 mesh/scale 변경 직후 남아 있을 수 있는 이전 전환 상태를 먼저 비운다.
    ResetRagdollBlendToPhysics();

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    // duration이 0 이하이거나 비정상 값이면 의도적으로 즉시 전환한다.
    // 이 경우 PhysicsScene write-back이 만든 pose가 그대로 적용된다.
    if (!Asset || Asset->Bones.empty() ||
        !std::isfinite(RagdollBlendToPhysicsDuration) ||
        RagdollBlendToPhysicsDuration <= 0.0f)
    {
        return;
    }

    RagdollBlendStartLocalPose.resize(Asset->Bones.size());
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
    {
        // write-back 최종 결과도 skeleton local pose이므로 시작점도 local space로 캡처한다.
        // world space에서 섞으면 parent/child 계층 재계산 과정에서 scale과 parent rotation 오차가 커진다.
        RagdollBlendStartLocalPose[BoneIndex] = GetBoneLocalTransformByIndex(BoneIndex);
    }

    RagdollBlendToPhysicsElapsed = 0.0f;
    bRagdollBlendToPhysicsActive = true;
}

void USkeletalMeshComponent::ResetRagdollBlendToPhysics()
{
    RagdollBlendStartLocalPose.clear();
    RagdollBlendToPhysicsElapsed = 0.0f;
    bRagdollBlendToPhysicsActive = false;
}

void USkeletalMeshComponent::ApplyRagdollBlendToPhysics(float DeltaTime, TArray<FTransform>& InOutPhysicsLocalPose)
{
    if (!bRagdollBlendToPhysicsActive)
    {
        return;
    }

    if (!std::isfinite(RagdollBlendToPhysicsDuration) ||
        RagdollBlendToPhysicsDuration <= 0.0f ||
        RagdollBlendStartLocalPose.size() != InOutPhysicsLocalPose.size())
    {
        // skeleton이 바뀌었거나 설정값이 깨진 경우에는 stale pose로 섞지 않는다.
        // 물리 포즈를 즉시 적용하는 쪽이 잘못된 배열을 보간하는 것보다 안전하다.
        ResetRagdollBlendToPhysics();
        return;
    }

    RagdollBlendToPhysicsElapsed += std::isfinite(DeltaTime) ? std::max(DeltaTime, 0.0f) : 0.0f;
    const float LinearAlpha = std::clamp(RagdollBlendToPhysicsElapsed / RagdollBlendToPhysicsDuration, 0.0f, 1.0f);
    const float BlendAlpha = SmoothStep01(LinearAlpha);

    // InOutPhysicsLocalPose는 PhysX body world pose를 skeleton local pose로 환산한 결과다.
    // 이 배열을 in-place로 수정하면 이후 SetBoneLocalTransforms 경로와 CPU/GPU skinning 경로를 그대로 재사용할 수 있다.
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(InOutPhysicsLocalPose.size()); ++BoneIndex)
    {
        InOutPhysicsLocalPose[BoneIndex] = BlendLocalTransform(
            RagdollBlendStartLocalPose[BoneIndex],
            InOutPhysicsLocalPose[BoneIndex],
            BlendAlpha);
    }

    if (LinearAlpha >= 1.0f)
    {
        // 완전히 physics pose로 넘어간 뒤에는 매 프레임 불필요한 배열 보간을 하지 않는다.
        ResetRagdollBlendToPhysics();
    }
}

void USkeletalMeshComponent::BeginRagdollBlendToAnimation()
{
    ResetRagdollBlendToAnimation();

    USkeletalMesh* Mesh = GetSkeletalMesh();
    FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    // 애니메이션으로 돌아갈 대상이 없거나 duration이 비정상이면 별도 복귀 blend 없이 즉시 animation tick에 맡긴다.
    if (!Asset || Asset->Bones.empty() ||
        !std::isfinite(RagdollBlendToAnimationDuration) ||
        RagdollBlendToAnimationDuration <= 0.0f)
    {
        return;
    }

    RagdollBlendToAnimationStartLocalPose.resize(Asset->Bones.size());
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
    {
        // 랙돌 중에는 PhysX write-back 결과가 이미 BoneEditLocalMatrices에 들어와 있다.
        // 따라서 현재 local transform을 저장하면 "끄는 순간 화면에 보이는 랙돌 자세"가 된다.
        RagdollBlendToAnimationStartLocalPose[BoneIndex] = GetBoneLocalTransformByIndex(BoneIndex);
    }

    RagdollBlendToAnimationElapsed = 0.0f;
    bRagdollBlendToAnimationActive = true;
}

void USkeletalMeshComponent::ResetRagdollBlendToAnimation()
{
    RagdollBlendToAnimationStartLocalPose.clear();
    RagdollBlendToAnimationElapsed = 0.0f;
    bRagdollBlendToAnimationActive = false;
}

bool USkeletalMeshComponent::TickRagdollBlendToAnimation(float DeltaTime)
{
    if (!bRagdollBlendToAnimationActive)
    {
        return false;
    }

    FPoseContext AnimationPose;
    if (!EvaluateAnimInstancePose(DeltaTime, AnimationPose))
    {
        ResetRagdollBlendToAnimation();
        return false;
    }

    if (!std::isfinite(RagdollBlendToAnimationDuration) ||
        RagdollBlendToAnimationDuration <= 0.0f ||
        RagdollBlendToAnimationStartLocalPose.size() != AnimationPose.Pose.size())
    {
        ResetRagdollBlendToAnimation();
        SetAnimationPose(AnimationPose.Pose, AnimationPose.MorphWeights);
        return true;
    }

    RagdollBlendToAnimationElapsed += std::isfinite(DeltaTime) ? std::max(DeltaTime, 0.0f) : 0.0f;
    const float LinearAlpha = std::clamp(RagdollBlendToAnimationElapsed / RagdollBlendToAnimationDuration, 0.0f, 1.0f);
    const float BlendAlpha = SmoothStep01(LinearAlpha);

    TArray<FTransform> BlendedPose;
    BlendedPose.resize(AnimationPose.Pose.size());
    for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(AnimationPose.Pose.size()); ++BoneIndex)
    {
        BlendedPose[BoneIndex] = BlendLocalTransform(
            RagdollBlendToAnimationStartLocalPose[BoneIndex],
            AnimationPose.Pose[BoneIndex],
            BlendAlpha);
    }

    SetAnimationPose(BlendedPose, AnimationPose.MorphWeights);

    if (LinearAlpha >= 1.0f)
    {
        ResetRagdollBlendToAnimation();
    }
    return true;
}

void USkeletalMeshComponent::SetAnimationMode(EAnimationMode InMode)
{
    if (AnimationMode == InMode) return;
    AnimationMode = InMode;
    InitializeAnimation();
}

bool USkeletalMeshComponent::CanUseAnimation(UAnimSequenceBase* InAsset) const
{
    if (!InAsset)
    {
        return true;
    }

    const USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh)
    {
        return false;
    }

    if (const UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        FSkeletonCompatibilityReport Report;
        const bool bCompatible = FAssetRegistry::CheckAnimationForMesh(Sequence, Mesh, &Report);
        if (!bCompatible)
        {
            UE_LOG("SetAnimation rejected: skeleton mismatch. Anim=%s Mesh=%s Reason=%s",
                Sequence->GetName().c_str(),
                Mesh->GetName().c_str(),
                Report.Reason.c_str());
        }
        return bCompatible;
    }

    return true;
}

void USkeletalMeshComponent::SetAnimation(UAnimSequenceBase* InAsset)
{
    if (!CanUseAnimation(InAsset))
    {
        return;
    }

    AnimationData.AnimToPlay = InAsset;

    if (UAnimSequence* Sequence = Cast<UAnimSequence>(InAsset))
    {
        AnimationData.AnimToPlayPath = Sequence->GetAssetPathFileName();
    }
    else if (!InAsset)
    {
        AnimationData.AnimToPlayPath = "None";
    }

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetAnimationAsset(InAsset);
    }
}

void USkeletalMeshComponent::SetPlayRate(float InRate)
{
    AnimationData.PlayRate = InRate;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetPlayRate(InRate);
    }
}

void USkeletalMeshComponent::SetLooping(bool bInLoop)
{
    AnimationData.bLooping = bInLoop;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetLooping(bInLoop);
    }
}

void USkeletalMeshComponent::SetPlaying(bool bInPlay)
{
    AnimationData.bPlaying = bInPlay;
    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        SingleNode->SetPlaying(bInPlay);
    }
}

void USkeletalMeshComponent::SetAnimInstanceClass(UClass* InClass)
{
    if (AnimInstanceClass.Get() == InClass) return;
    AnimInstanceClass = InClass;   // TSubclassOf 가 IsA 가드로 검증 (잘못된 클래스 → nullptr).
    if (AnimationMode == EAnimationMode::AnimationCustom)
    {
        InitializeAnimation();
    }
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
    if (AnimInstance == InInstance) return;
    ClearAnimInstance();
    AnimInstance = InInstance;
    if (AnimInstance)
    {
        AnimInstance->SetOuter(this);
        AnimInstance->SetOwningComponent(this);
        AnimInstance->NativeInitializeAnimation();
    }
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetAnimNodeInstance(FName NodeName) const
{
    (void)NodeName;
    return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::LoadAnimationFromPath()
{
    AnimationData.AnimToPlay = nullptr;

    if (AnimationData.AnimToPlayPath.empty() || AnimationData.AnimToPlayPath == "None")
    {
        return;
    }

    UAnimSequence* LoadedAnimation = FAnimationManager::Get().LoadAnimation(AnimationData.AnimToPlayPath.ToString());
    if (LoadedAnimation && CanUseAnimation(LoadedAnimation))
    {
        AnimationData.AnimToPlay = LoadedAnimation;
    }
    else
    {
        AnimationData.AnimToPlay = nullptr;
    }
}

void USkeletalMeshComponent::InitializeAnimation()
{
    if (!GetSkeletalMesh())
    {
        ClearAnimInstance();
        return;
    }
    if (AnimationMode == EAnimationMode::None)
    {
        ClearAnimInstance();
        return;
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode &&
        !AnimationData.AnimToPlay &&
        !AnimationData.AnimToPlayPath.empty() &&
        AnimationData.AnimToPlayPath != "None")
    {
        LoadAnimationFromPath();
    }

    if (AnimationMode == EAnimationMode::AnimationSingleNode && !CanUseAnimation(AnimationData.AnimToPlay))
    {
        AnimationData.AnimToPlay = nullptr;
        AnimationData.AnimToPlayPath = "None";
    }

    switch (AnimationMode)
    {
    case EAnimationMode::AnimationSingleNode:
    {
        ClearAnimInstance();

        UAnimSingleNodeInstance* Single =
            UObjectManager::Get().CreateObject<UAnimSingleNodeInstance>(this);
        AnimInstance = Single;
        Single->SetOwningComponent(this);
        Single->SetAnimationAsset(AnimationData.AnimToPlay);
        Single->SetPlayRate(AnimationData.PlayRate);
        Single->SetLooping(AnimationData.bLooping);
        Single->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        Single->NativeInitializeAnimation();
        break;
    }
    case EAnimationMode::AnimationCustom:
    {
        UClass* DesiredClass = AnimInstanceClass.Get();
        if (!DesiredClass)
        {
            ClearAnimInstance();
            return;
        }

        if (AnimInstance && AnimInstance->GetClass() == DesiredClass)
        {
            AnimInstance->SetOuter(this);
            AnimInstance->SetOwningComponent(this);
            AnimInstance->NativeInitializeAnimation();
            break;
        }

        ClearAnimInstance();

        UObject* Obj = FObjectFactory::Get().Create(DesiredClass->GetName(), this);
        AnimInstance = Cast<UAnimInstance>(Obj);
		if (!AnimInstance)
        {
            // 클래스가 등록 안됐거나 캐스트 실패 — 무관한 객체가 생성됐을 수 있으니 정리.
            if (Obj) UObjectManager::Get().DestroyObject(Obj);
            return;
        }
        AnimInstance->SetOwningComponent(this);

        AnimInstance->NativeInitializeAnimation();
        break;
    }
    default:
        break;
    }
}

void USkeletalMeshComponent::ClearAnimInstance()
{
    ResetRagdollBlendToAnimation();
    if (AnimInstance)
    {
        UObjectManager::Get().DestroyObject(AnimInstance);
        AnimInstance = nullptr;
    }
}

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
    if (TickRagdollBlendToAnimation(DeltaTime))
    {
        UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
        CaptureRagdollVelocityPose(DeltaTime);
        return;
    }

    if (bRagdollSimulating && !Bodies.empty())
    {
        Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
        return;
    }

    if (EvaluateAnimInstance(DeltaTime))
    {
        UMeshComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
        CaptureRagdollVelocityPose(DeltaTime);
        return;
    }

    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    CaptureRagdollVelocityPose(DeltaTime);
}

// ──────────────────────────────────────────────
// Editor / 직렬화 통합
// ──────────────────────────────────────────────
void USkeletalMeshComponent::GetEditableProperties(TArray<FPropertyValue>& OutProps)
{
    Super::GetEditableProperties(OutProps);

    // AnimInstance 자체 properties (Speed 등) 도 패널에 같이 노출 — 컴포넌트가 forward.
    // 자식이 자기 카테고리(예: "Animation|Character") 로 그룹화.
    if (AnimInstance) AnimInstance->GetEditableProperties(OutProps);
}

void USkeletalMeshComponent::PostEditProperty(const char* PropertyName)
{
    Super::PostEditProperty(PropertyName);
    if (!PropertyName) return;

    if (std::strcmp(PropertyName, "AnimationMode") == 0)
    {
        InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimInstanceClass") == 0)
    {
        // 클래스 슬롯이 바뀌면 Custom 모드에서 인스턴스 재생성 필요. (ours — Phase 6)
        if (AnimationMode == EAnimationMode::AnimationCustom) InitializeAnimation();
    }
    else if (std::strcmp(PropertyName, "AnimationData") == 0)
    {
        LoadAnimationFromPath();

        if (AnimInstance)
        {
            InitializeAnimation();
        }
    }
    else if (std::strcmp(PropertyName, "AnimToPlayPath") == 0)
    {
        // theirs (main): FAnimationManager 가 path 로 실제 UAnimSequence 로딩 — Phase 4 의 TODO 해소.
        // Mode 가 None 이면 SingleNode 로 자동 전환, AnimInstance 없으면 Initialize, 있으면 SingleNode setter 들 갱신.
        LoadAnimationFromPath();

        if (AnimationMode == EAnimationMode::None)
        {
            AnimationMode = EAnimationMode::AnimationSingleNode;
        }

        if (!AnimInstance)
        {
            InitializeAnimation();
        }
        else if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
        {
            if (!CanUseAnimation(AnimationData.AnimToPlay))
            {
                AnimationData.AnimToPlay = nullptr;
                AnimationData.AnimToPlayPath = "None";
            }
            SingleNode->SetAnimationAsset(AnimationData.AnimToPlay);
            SingleNode->SetPlayRate(AnimationData.PlayRate);
            SingleNode->SetLooping(AnimationData.bLooping);
            SingleNode->SetPlaying(AnimationData.bPlaying && AnimationData.AnimToPlay != nullptr);
        }
    }
    else if (std::strcmp(PropertyName, "PlayRate") == 0)
    {
        SetPlayRate(AnimationData.PlayRate);
    }
    else if (std::strcmp(PropertyName, "bLooping") == 0)
    {
        SetLooping(AnimationData.bLooping);
    }
    else if (std::strcmp(PropertyName, "bPlaying") == 0)
    {
        SetPlaying(AnimationData.bPlaying);
    }

    // AnimInstance 자체 properties 는 자식이 자체 PostEdit 처리. 컴포넌트는 dispatch 만.
    // 컴포넌트가 인식한 이름과 겹치지 않는 한 무해 (자식이 모르는 이름은 no-op).
    if (AnimInstance) AnimInstance->PostEditProperty(PropertyName);
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
    Super::Serialize(Ar);

    uint8 ModeRaw = static_cast<uint8>(AnimationMode);
    Ar << ModeRaw;
    AnimationMode = static_cast<EAnimationMode>(ModeRaw);

    // AnimToPlay 의 path 만 라운드트립. 실제 포인터 복원은 InitializeAnimation() → LoadAnimationFromPath() 가 처리.
    FString AnimToPlayPath = Ar.IsSaving() ? AnimationData.AnimToPlayPath.ToString() : FString();
    Ar << AnimToPlayPath;
    if (Ar.IsLoading())
    {
        AnimationData.AnimToPlayPath.SetPath(AnimToPlayPath);
    }
    Ar << AnimationData.PlayRate;
    Ar << AnimationData.bLooping;
    Ar << AnimationData.bPlaying;

}

bool USkeletalMeshComponent::EvaluateAnimInstancePose(float DeltaTime, FPoseContext& OutPose)
{
    if (!AnimInstance) return false;

    USkeletalMesh* Mesh = GetSkeletalMesh();
    if (!Mesh) return false;
    FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
    if (!Asset || Asset->Bones.empty()) return false;

    if (UAnimSingleNodeInstance* SingleNode = Cast<UAnimSingleNodeInstance>(AnimInstance))
    {
        if (!CanUseAnimation(SingleNode->GetAnimationAsset()))
        {
            SingleNode->SetAnimationAsset(nullptr);
            return false;
        }
    }

    AnimInstance->UpdateAnimation(DeltaTime);

    // Root motion 적용은 UCharacterMovementComponent 가 책임.
    // CMC::TickComponent (TG_DuringPhysics) 가 매 frame 이 AnimInstance->ConsumeRootMotion 으로
    // 누적값을 가져가 capsule 이동 / 회전에 반영한다 (sweep / floor stick 통과).
    // Mesh 는 actor transform 을 직접 만지지 않는다 — UE 본가 패턴.
    //
    // 주의: CMC 가 없는 actor 에 root motion 켠 anim 을 붙이면 누적값이 anywhere 도
    // 소비되지 않아 in-place 로 보인다. ACharacter 외 케이스에서 root motion 이 필요해지면
    // 별도 소비 경로가 추가되어야 한다.

    OutPose.SkeletalMesh = Mesh;
    OutPose.Pose.resize(Asset->Bones.size());
    OutPose.ResetToRefPose();
    AnimInstance->EvaluatePose(OutPose);
    return true;
}

bool USkeletalMeshComponent::EvaluateAnimInstance(float DeltaTime)
{
    FPoseContext Out;
    if (!EvaluateAnimInstancePose(DeltaTime, Out))
    {
        return false;
    }

    SetAnimationPose(Out.Pose, Out.MorphWeights);
    return true;
}
