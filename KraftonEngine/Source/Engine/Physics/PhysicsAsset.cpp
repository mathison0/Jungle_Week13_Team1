#include "Physics/PhysicsAsset.h"

#include "Core/Logging/Log.h"
#include "Math/MathUtils.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Object/Object.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cctype>
#include <cmath>

enum class EGeneratedBodyShape : uint8
{
    Sphere,
    Box,
    Capsule
};

struct FBoneVertexSample
{
    FVector LocalPosition;
};

static FString ToLowerAscii(FString Value)
{
    for (char& Ch : Value)
    {
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    }
    return Value;
}

static bool ContainsToken(const FString& LowerName, const char* Token)
{
    return LowerName.find(Token) != FString::npos;
}

static bool ShouldSkipBoneName(const FString& BoneName)
{
    const FString LowerName = ToLowerAscii(BoneName);
    return ContainsToken(LowerName, "ik")
        || ContainsToken(LowerName, "ctrl")
        || ContainsToken(LowerName, "control")
        || ContainsToken(LowerName, "dummy")
        || ContainsToken(LowerName, "helper")
        || ContainsToken(LowerName, "socket")
        || ContainsToken(LowerName, "twist")
        || ContainsToken(LowerName, "roll")
        || ContainsToken(LowerName, "nub")
        || ContainsToken(LowerName, "end")
        || ContainsToken(LowerName, "finger")
        || ContainsToken(LowerName, "thumb")
        || ContainsToken(LowerName, "toe")
        || ContainsToken(LowerName, "hair")
        || ContainsToken(LowerName, "cloth")
        || ContainsToken(LowerName, "skirt")
        || ContainsToken(LowerName, "dress")
        || ContainsToken(LowerName, "ribbon")
        || ContainsToken(LowerName, "cape")
        || ContainsToken(LowerName, "weapon");
}

static bool IsTerminalDetailAnchorBoneName(const FString& BoneName)
{
    const FString LowerName = ToLowerAscii(BoneName);
    return ContainsToken(LowerName, "hand")
        || ContainsToken(LowerName, "foot");
}

static bool ShouldSkipTerminalDetailBone(const FSkeletalMesh& Mesh, int32 BoneIndex)
{
    const int32 BoneCount = static_cast<int32>(Mesh.Bones.size());
    if (BoneIndex < 0 || BoneIndex >= BoneCount)
    {
        return false;
    }

    int32 ParentIndex = Mesh.Bones[BoneIndex].ParentIndex;
    for (int32 Depth = 0; Depth < BoneCount && ParentIndex >= 0 && ParentIndex < BoneCount; ++Depth)
    {
        if (IsTerminalDetailAnchorBoneName(Mesh.Bones[ParentIndex].Name))
        {
            return true;
        }
        ParentIndex = Mesh.Bones[ParentIndex].ParentIndex;
    }

    return false;
}

static bool IsFiniteVector(const FVector& Value)
{
    return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
}

static float Percentile(TArray<float> Values, float Alpha)
{
    if (Values.empty())
    {
        return 0.0f;
    }

    std::sort(Values.begin(), Values.end());

    const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
    const float ScaledIndex = ClampedAlpha * static_cast<float>(Values.size() - 1);
    const int32 LowerIndex = static_cast<int32>(std::floor(ScaledIndex));
    const int32 UpperIndex = static_cast<int32>(std::ceil(ScaledIndex));
    const float Blend = ScaledIndex - static_cast<float>(LowerIndex);

    if (LowerIndex == UpperIndex)
    {
        return Values[LowerIndex];
    }

    return FMath::Lerp(Values[LowerIndex], Values[UpperIndex], Blend);
}

static FVector MaxVector(const FVector& Value, float MinValue)
{
    return FVector(
        std::max(Value.X, MinValue),
        std::max(Value.Y, MinValue),
        std::max(Value.Z, MinValue));
}

// 점 집합의 AABB 크기(max-min)를 반환. 비었으면 0. (merge-up 본 크기 판정/볼륨 정렬용)
static FVector ComputeSampleExtent(const TArray<FVector>& Points)
{
    if (Points.empty())
    {
        return FVector::ZeroVector;
    }
    FVector Min = Points[0];
    FVector Max = Points[0];
    for (const FVector& P : Points)
    {
        Min.X = std::min(Min.X, P.X); Min.Y = std::min(Min.Y, P.Y); Min.Z = std::min(Min.Z, P.Z);
        Max.X = std::max(Max.X, P.X); Max.Y = std::max(Max.Y, P.Y); Max.Z = std::max(Max.Z, P.Z);
    }
    return Max - Min;
}

static void BuildPercentileBounds(
    const TArray<FBoneVertexSample>& Samples,
    float LowerPercentile,
    float UpperPercentile,
    FVector& OutMin,
    FVector& OutMax)
{
    TArray<float> Xs;
    TArray<float> Ys;
    TArray<float> Zs;
    Xs.reserve(Samples.size());
    Ys.reserve(Samples.size());
    Zs.reserve(Samples.size());

    for (const FBoneVertexSample& Sample : Samples)
    {
        Xs.push_back(Sample.LocalPosition.X);
        Ys.push_back(Sample.LocalPosition.Y);
        Zs.push_back(Sample.LocalPosition.Z);
    }

    OutMin = FVector(
        Percentile(Xs, LowerPercentile),
        Percentile(Ys, LowerPercentile),
        Percentile(Zs, LowerPercentile));
    OutMax = FVector(
        Percentile(Xs, UpperPercentile),
        Percentile(Ys, UpperPercentile),
        Percentile(Zs, UpperPercentile));
}

static FVector GetLongestLocalAxis(const FVector& Extent)
{
    if (Extent.X >= Extent.Y && Extent.X >= Extent.Z)
    {
        return FVector::XAxisVector;
    }
    if (Extent.Y >= Extent.X && Extent.Y >= Extent.Z)
    {
        return FVector::YAxisVector;
    }
    return FVector::ZAxisVector;
}

static FQuat MakeQuatFromZAxis(const FVector& InAxis)
{
    FVector Axis = InAxis;
    if (Axis.IsNearlyZero())
    {
        return FQuat::Identity;
    }
    Axis.Normalize();

    const FVector From = FVector::ZAxisVector;
    const float Dot = FMath::Clamp(From.Dot(Axis), -1.0f, 1.0f);
    if (Dot > 0.9999f)
    {
        return FQuat::Identity;
    }
    if (Dot < -0.9999f)
    {
        return FQuat::FromAxisAngle(FVector::XAxisVector, FMath::Pi);
    }

    FVector RotationAxis = From.Cross(Axis);
    RotationAxis.Normalize();

    FQuat Result = FQuat::FromAxisAngle(RotationAxis, std::acos(Dot));
    Result.Normalize();
    return Result;
}

static bool IsSpherePreferredBoneName(const FString& BoneName)
{
    const FString LowerName = ToLowerAscii(BoneName);
    return ContainsToken(LowerName, "head");
}

static bool IsBoxPreferredBoneName(const FString& BoneName)
{
    const FString LowerName = ToLowerAscii(BoneName);
    return ContainsToken(LowerName, "pelvis")
        || ContainsToken(LowerName, "hip")
        || ContainsToken(LowerName, "spine")
        || ContainsToken(LowerName, "chest")
        || ContainsToken(LowerName, "torso")
        || ContainsToken(LowerName, "hand")
        || ContainsToken(LowerName, "foot");
}

static EGeneratedBodyShape ChooseBodyShape(const FString& BoneName, bool bHasUsableAxis)
{
    if (IsSpherePreferredBoneName(BoneName))
    {
        return EGeneratedBodyShape::Sphere;
    }
    if (IsBoxPreferredBoneName(BoneName))
    {
        return EGeneratedBodyShape::Box;
    }
    return bHasUsableAxis ? EGeneratedBodyShape::Capsule : EGeneratedBodyShape::Box;
}

static const char* ToShapeLogName(EGeneratedBodyShape Shape)
{
    switch (Shape)
    {
    case EGeneratedBodyShape::Sphere:
        return "Sphere";
    case EGeneratedBodyShape::Box:
        return "Box";
    case EGeneratedBodyShape::Capsule:
        return "Capsule";
    default:
        return "Unknown";
    }
}

static int32 FindBestChildBone(
    const FSkeletalMesh& Mesh,
    const TArray<TArray<int32>>& ChildrenByBone,
    int32 BoneIndex,
    const FPhysicsAssetAutoGenerateSettings& Settings)
{
    int32 BestChild = -1;
    float BestDistanceSq = 0.0f;

    const FMatrix BoneBindInverse = Mesh.Bones[BoneIndex].GetInverseBindPose();
    for (const int32 ChildIndex : ChildrenByBone[BoneIndex])
    {
        if (ChildIndex < 0 || ChildIndex >= static_cast<int32>(Mesh.Bones.size()))
        {
            continue;
        }
        if (Settings.bUseDefaultNameFilters && ShouldSkipBoneName(Mesh.Bones[ChildIndex].Name))
        {
            continue;
        }
        if (Settings.bUseDefaultNameFilters && ShouldSkipTerminalDetailBone(Mesh, ChildIndex))
        {
            continue;
        }

        const FVector ChildLocal = Mesh.Bones[ChildIndex].GetSkinBindGlobalPose().GetLocation() * BoneBindInverse;
        const float DistanceSq = ChildLocal.LengthSquared();
        if (DistanceSq > BestDistanceSq)
        {
            BestDistanceSq = DistanceSq;
            BestChild = ChildIndex;
        }
    }

    return BestChild;
}

static bool HasConstraint(const TArray<FConstraintSetup>& Constraints, const FName& ParentBoneName, const FName& ChildBoneName)
{
    for (const FConstraintSetup& Constraint : Constraints)
    {
        if (Constraint.ParentBoneName == ParentBoneName && Constraint.ChildBoneName == ChildBoneName)
        {
            return true;
        }
    }
    return false;
}

void UPhysicsAsset::Serialize(FArchive& Ar)
{
    SerializeProperties(Ar, PF_Save);

    for (UBodySetup* BodySetup : BodySetups)
    {
        if (BodySetup)
        {
            BodySetup->SetOuter(this);
        }
    }
}

bool UPhysicsAsset::HasAnyBodySetup() const
{
    for (const UBodySetup* BodySetup : BodySetups)
    {
        if (BodySetup && BodySetup->HasGeometry())
        {
            return true;
        }
    }
    return false;
}

bool UPhysicsAsset::HasAnyConstraintSetup() const
{
    return !ConstraintSetups.empty();
}

int32 UPhysicsAsset::FindBodySetupIndexByBoneName(const FName& BoneName) const
{
    for (int32 Index = 0; Index < static_cast<int32>(BodySetups.size()); ++Index)
    {
        const UBodySetup* BodySetup = BodySetups[Index];
        if (BodySetup && BodySetup->GetBoneName() == BoneName)
        {
            return Index;
        }
    }
    return -1;
}

UBodySetup* UPhysicsAsset::FindBodySetupByBoneName(const FName& BoneName) const
{
    const int32 Index = FindBodySetupIndexByBoneName(BoneName);
    return Index >= 0 ? BodySetups[Index] : nullptr;
}

bool UPhysicsAsset::AutoGeneratePrimitiveBodiesFromSkeletalMesh(
    const FSkeletalMesh& Mesh,
    const FPhysicsAssetAutoGenerateSettings& Settings,
    FPhysicsAssetAutoGenerateStats* OutStats)
{
    FPhysicsAssetAutoGenerateStats Stats;

    const int32 BoneCount = static_cast<int32>(Mesh.Bones.size());
    if (BoneCount <= 0 || Mesh.Vertices.empty())
    {
        UE_LOG("[PhysicsAssetAutoGen] Aborted: bones=%d vertices=%d",
            BoneCount,
            static_cast<int32>(Mesh.Vertices.size()));
        if (OutStats) *OutStats = Stats;
        return false;
    }

    if (Settings.bReplaceExisting)
    {
        UE_LOG("[PhysicsAssetAutoGen] Replacing existing setup: bodies=%d constraints=%d",
            static_cast<int32>(BodySetups.size()),
            static_cast<int32>(ConstraintSetups.size()));
        for (UBodySetup* BodySetup : BodySetups)
        {
            if (BodySetup)
            {
                UObjectManager::Get().DestroyObject(BodySetup);
            }
        }
        BodySetups.clear();
        ConstraintSetups.clear();
    }

    const float LowerPercentile = FMath::Clamp(Settings.LowerPercentile, 0.0f, 0.49f);
    const float UpperPercentile = FMath::Clamp(Settings.UpperPercentile, LowerPercentile + 0.01f, 1.0f);
    const float MinShapeSize = std::max(Settings.MinShapeSize, 0.001f);
    const float ShapePadding = std::max(Settings.ShapePadding, 1.0f);
    const int32 RequiredMinVertexCount = std::max(Settings.MinVertexCount, 1);

    UE_LOG(
        "[PhysicsAssetAutoGen] Start: bones=%d vertices=%d replace=%s constraints=%s dominantOnly=%s filters=%s minWeight=%.3f percentile=%.2f-%.2f padding=%.2f minSize=%.3f minVerts=%d",
        BoneCount,
        static_cast<int32>(Mesh.Vertices.size()),
        Settings.bReplaceExisting ? "true" : "false",
        Settings.bCreateConstraints ? "true" : "false",
        Settings.bUseDominantBoneOnly ? "true" : "false",
        Settings.bUseDefaultNameFilters ? "true" : "false",
        Settings.MinBoneWeight,
        LowerPercentile,
        UpperPercentile,
        ShapePadding,
        MinShapeSize,
        RequiredMinVertexCount);

    // Imported vertices live in skin bind space. Reference pose can differ from
    // cluster bind pose (commonly by an FBX axis conversion), so localize with
    // the inverse bind matrix used by skinning.
    TArray<FMatrix> BoneBindInverse;
    BoneBindInverse.resize(BoneCount);
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        BoneBindInverse[BoneIndex] = Mesh.Bones[BoneIndex].GetInverseBindPose();
    }

    TArray<TArray<int32>> ChildrenByBone;
    ChildrenByBone.resize(BoneCount);
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const int32 ParentIndex = Mesh.Bones[BoneIndex].ParentIndex;
        if (ParentIndex >= 0 && ParentIndex < BoneCount)
        {
            ChildrenByBone[ParentIndex].push_back(BoneIndex);
        }
    }

    // [merge-up] 본별 직접 가중 버텍스를 COMPONENT 공간으로 수집한다(병합이 단순 concat이 되도록).
    // 본-로컬 변환은 실제 바디를 만드는 시점에만 적용한다. 동시에 메시 전체 AABB로 본 크기 상대비율을 구한다.
    TArray<TArray<FBoneVertexSample>> SamplesByBone;
    SamplesByBone.resize(BoneCount);

    TArray<TArray<FVector>> CompSamplesByBone;
    CompSamplesByBone.resize(BoneCount);

    FVector MeshMin(0.0f, 0.0f, 0.0f);
    FVector MeshMax(0.0f, 0.0f, 0.0f);
    bool bMeshBoundsValid = false;

    for (const FVertexPNCTBW& Vertex : Mesh.Vertices)
    {
        if (!IsFiniteVector(Vertex.Position))
        {
            continue;
        }

        if (!bMeshBoundsValid)
        {
            MeshMin = Vertex.Position;
            MeshMax = Vertex.Position;
            bMeshBoundsValid = true;
        }
        else
        {
            MeshMin.X = std::min(MeshMin.X, Vertex.Position.X);
            MeshMin.Y = std::min(MeshMin.Y, Vertex.Position.Y);
            MeshMin.Z = std::min(MeshMin.Z, Vertex.Position.Z);
            MeshMax.X = std::max(MeshMax.X, Vertex.Position.X);
            MeshMax.Y = std::max(MeshMax.Y, Vertex.Position.Y);
            MeshMax.Z = std::max(MeshMax.Z, Vertex.Position.Z);
        }

        if (Settings.bUseDominantBoneOnly)
        {
            int32 BestBoneIndex = -1;
            float BestWeight = 0.0f;
            for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
            {
                const int32 BoneIndex = Vertex.BoneIndices[InfluenceIndex];
                const float Weight = Vertex.BoneWeights[InfluenceIndex];
                if (BoneIndex >= 0 && BoneIndex < BoneCount && Weight > BestWeight)
                {
                    BestBoneIndex = BoneIndex;
                    BestWeight = Weight;
                }
            }

            if (BestBoneIndex >= 0 && BestWeight >= Settings.MinBoneWeight)
            {
                CompSamplesByBone[BestBoneIndex].push_back(Vertex.Position);
            }
        }
        else
        {
            for (int32 InfluenceIndex = 0; InfluenceIndex < 4; ++InfluenceIndex)
            {
                const int32 BoneIndex = Vertex.BoneIndices[InfluenceIndex];
                const float Weight = Vertex.BoneWeights[InfluenceIndex];
                if (BoneIndex >= 0 && BoneIndex < BoneCount && Weight >= Settings.MinBoneWeight)
                {
                    CompSamplesByBone[BoneIndex].push_back(Vertex.Position);
                }
            }
        }
    }

    const FVector MeshSize = bMeshBoundsValid ? (MeshMax - MeshMin) : FVector::ZeroVector;
    const float MeshExtentMax = std::max({ MeshSize.X, MeshSize.Y, MeshSize.Z });

    // 본 깊이(루트 기준). bone 배열은 parent-first 전제(parent index < child index).
    TArray<int32> BoneDepth;
    BoneDepth.resize(BoneCount, 0);
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const int32 Parent = Mesh.Bones[BoneIndex].ParentIndex;
        BoneDepth[BoneIndex] = (Parent >= 0 && Parent < BoneIndex) ? BoneDepth[Parent] + 1 : 0;
    }

    // 이미 바디가 있는 본(편집된 에셋 보존).
    TArray<bool> bHasBodyForBone;
    bHasBodyForBone.resize(BoneCount, false);
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        if (FindBodySetupIndexByBoneName(FName(Mesh.Bones[BoneIndex].Name)) >= 0)
        {
            bHasBodyForBone[BoneIndex] = true;
        }
    }

    // [merge-up] leaf-first 순회로 작은/이름필터/깊은/샘플부족 본을 드롭하지 않고 부모로 샘플을 합친다.
    // 부모 바디가 자식 지오메트리를 흡수해 고아 영역이 없고 모양이 정확하다(UE PhAT 방식).
    const float MinBoneSizeRatio = std::max(Settings.MinBoneSizeRatio, 0.0f);
    TArray<bool> bGetsBody;
    bGetsBody.resize(BoneCount, false);
    for (int32 BoneIndex = BoneCount - 1; BoneIndex >= 0; --BoneIndex)
    {
        const FBone& Bone = Mesh.Bones[BoneIndex];
        const int32 Parent = Bone.ParentIndex;

        if (bHasBodyForBone[BoneIndex])
        {
            continue; // 기존 바디는 유지(신규 생성/병합 대상 아님)
        }

        const FVector SampleSize = ComputeSampleExtent(CompSamplesByBone[BoneIndex]);
        const float SampleExtentMax = std::max({ SampleSize.X, SampleSize.Y, SampleSize.Z });
        const float SizeRatio = (MeshExtentMax > 1.0e-6f) ? (SampleExtentMax / MeshExtentMax) : 0.0f;

        const bool bNameFiltered = Settings.bUseDefaultNameFilters
            && (ShouldSkipBoneName(Bone.Name) || ShouldSkipTerminalDetailBone(Mesh, BoneIndex));
        const bool bTooSmall = SizeRatio < MinBoneSizeRatio;
        const bool bTooDeep  = Settings.MaxBoneDepth > 0 && BoneDepth[BoneIndex] > Settings.MaxBoneDepth;
        const bool bTooFew   = CompSamplesByBone[BoneIndex].size() < static_cast<size_t>(RequiredMinVertexCount);

        if (Parent >= 0 && (bNameFiltered || bTooSmall || bTooDeep || bTooFew))
        {
            TArray<FVector>& ParentSamples = CompSamplesByBone[Parent];
            ParentSamples.insert(ParentSamples.end(),
                CompSamplesByBone[BoneIndex].begin(), CompSamplesByBone[BoneIndex].end());
            ++Stats.SkippedBoneCount;
            UE_LOG("[PhysicsAssetAutoGen] Merge bone=%s -> parent=%s reason=%s ratio=%.3f depth=%d samples=%d",
                Bone.Name.c_str(),
                Mesh.Bones[Parent].Name.c_str(),
                bNameFiltered ? "NameFilter" : (bTooDeep ? "Depth" : (bTooSmall ? "Size" : "FewSamples")),
                SizeRatio,
                BoneDepth[BoneIndex],
                static_cast<int32>(CompSamplesByBone[BoneIndex].size()));
            continue;
        }

        bGetsBody[BoneIndex] = true;
    }

    // [merge-up] MaxBodyCount 안전캡: 초과 시 볼륨 작은 순으로 가장 가까운 바디 조상에 병합.
    if (Settings.MaxBodyCount > 0)
    {
        struct FBodyCandidate { int32 BoneIndex; float Volume; };
        TArray<FBodyCandidate> Candidates;
        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            if (!bGetsBody[BoneIndex])
            {
                continue;
            }
            const FVector S = ComputeSampleExtent(CompSamplesByBone[BoneIndex]);
            Candidates.push_back({ BoneIndex, S.X * S.Y * S.Z });
        }
        if (static_cast<int32>(Candidates.size()) > Settings.MaxBodyCount)
        {
            std::sort(Candidates.begin(), Candidates.end(),
                [](const FBodyCandidate& A, const FBodyCandidate& B) { return A.Volume < B.Volume; });
            const int32 DemoteCount = static_cast<int32>(Candidates.size()) - Settings.MaxBodyCount;
            for (int32 i = 0; i < DemoteCount; ++i)
            {
                const int32 BoneIndex = Candidates[i].BoneIndex;
                int32 Ancestor = Mesh.Bones[BoneIndex].ParentIndex;
                while (Ancestor >= 0 && !bGetsBody[Ancestor] && !bHasBodyForBone[Ancestor])
                {
                    Ancestor = Mesh.Bones[Ancestor].ParentIndex;
                }
                bGetsBody[BoneIndex] = false;
                if (Ancestor >= 0)
                {
                    TArray<FVector>& Dst = CompSamplesByBone[Ancestor];
                    Dst.insert(Dst.end(),
                        CompSamplesByBone[BoneIndex].begin(), CompSamplesByBone[BoneIndex].end());
                }
                ++Stats.SkippedBoneCount;
                UE_LOG("[PhysicsAssetAutoGen] Cap merge bone=%s (over MaxBodyCount=%d)",
                    Mesh.Bones[BoneIndex].Name.c_str(),
                    Settings.MaxBodyCount);
            }
        }
    }

    // 바디 본의 병합된 component 샘플을 본-로컬로 변환해 기존 사이징 로직에 넘긴다.
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        if (!bGetsBody[BoneIndex])
        {
            continue;
        }
        const FMatrix& InvBind = BoneBindInverse[BoneIndex];
        TArray<FBoneVertexSample>& Localized = SamplesByBone[BoneIndex];
        Localized.reserve(CompSamplesByBone[BoneIndex].size());
        for (const FVector& P : CompSamplesByBone[BoneIndex])
        {
            Localized.push_back({ P * InvBind });
        }
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const FBone& Bone = Mesh.Bones[BoneIndex];
        const FName BoneName(Bone.Name);
        const TArray<FBoneVertexSample>& Samples = SamplesByBone[BoneIndex];

        if (!bGetsBody[BoneIndex])
        {
            continue;
        }
        if (Samples.size() < static_cast<size_t>(RequiredMinVertexCount))
        {
            UE_LOG("[PhysicsAssetAutoGen] Skip bone=%s reason=InsufficientSamples samples=%d required=%d",
                Bone.Name.c_str(),
                static_cast<int32>(Samples.size()),
                RequiredMinVertexCount);
            ++Stats.SkippedBoneCount;
            continue;
        }

        FVector LocalMin;
        FVector LocalMax;
        BuildPercentileBounds(Samples, LowerPercentile, UpperPercentile, LocalMin, LocalMax);

        const FVector Center = (LocalMin + LocalMax) * 0.5f;
        const FVector Extent = MaxVector((LocalMax - LocalMin) * (0.5f * ShapePadding), MinShapeSize);
        if (!IsFiniteVector(Center) || !IsFiniteVector(Extent))
        {
            UE_LOG("[PhysicsAssetAutoGen] Skip bone=%s reason=InvalidBounds samples=%d",
                Bone.Name.c_str(),
                static_cast<int32>(Samples.size()));
            ++Stats.SkippedBoneCount;
            continue;
        }

        const int32 ChildIndex = FindBestChildBone(Mesh, ChildrenByBone, BoneIndex, Settings);
        FVector CapsuleAxis = GetLongestLocalAxis(Extent);
        bool bHasUsableAxis = false;
        if (ChildIndex >= 0)
        {
            CapsuleAxis = Mesh.Bones[ChildIndex].GetSkinBindGlobalPose().GetLocation() * BoneBindInverse[BoneIndex];
            bHasUsableAxis = !CapsuleAxis.IsNearlyZero(MinShapeSize);
            if (bHasUsableAxis)
            {
                CapsuleAxis.Normalize();
            }
        }
        else
        {
            bHasUsableAxis = !CapsuleAxis.IsNearlyZero();
        }

        UBodySetup* BodySetup = UObjectManager::Get().CreateObject<UBodySetup>(this);
        BodySetup->SetBoneName(BoneName);

        const EGeneratedBodyShape Shape = ChooseBodyShape(Bone.Name, bHasUsableAxis);
        if (Shape == EGeneratedBodyShape::Sphere)
        {
            TArray<float> Distances;
            Distances.reserve(Samples.size());
            for (const FBoneVertexSample& Sample : Samples)
            {
                Distances.push_back((Sample.LocalPosition - Center).Length());
            }

            FKSphereElem Sphere;
            Sphere.Name = Bone.Name + " Auto Sphere";
            Sphere.Radius = std::max(Percentile(Distances, UpperPercentile) * ShapePadding, MinShapeSize);
            Sphere.Transform.Location = Center;
            BodySetup->GetAggGeom().SphereElems.push_back(Sphere);

            UE_LOG("[PhysicsAssetAutoGen] Body bone=%s shape=%s samples=%d center=(%.3f,%.3f,%.3f) radius=%.3f",
                Bone.Name.c_str(),
                ToShapeLogName(Shape),
                static_cast<int32>(Samples.size()),
                Sphere.Transform.Location.X,
                Sphere.Transform.Location.Y,
                Sphere.Transform.Location.Z,
                Sphere.Radius);
        }
        else if (Shape == EGeneratedBodyShape::Capsule && bHasUsableAxis)
        {
            TArray<float> Projections;
            Projections.reserve(Samples.size());
            for (const FBoneVertexSample& Sample : Samples)
            {
                Projections.push_back(Sample.LocalPosition.Dot(CapsuleAxis));
            }

            const float MinProjection = Percentile(Projections, LowerPercentile);
            const float MaxProjection = Percentile(Projections, UpperPercentile);
            const float CenterProjection = (MinProjection + MaxProjection) * 0.5f;

            FVector PerpCenter = FVector::ZeroVector;
            int32 PerpCount = 0;
            for (const FBoneVertexSample& Sample : Samples)
            {
                const float Projection = Sample.LocalPosition.Dot(CapsuleAxis);
                if (Projection < MinProjection || Projection > MaxProjection)
                {
                    continue;
                }
                PerpCenter += Sample.LocalPosition - CapsuleAxis * Projection;
                ++PerpCount;
            }
            if (PerpCount > 0)
            {
                PerpCenter /= static_cast<float>(PerpCount);
            }

            TArray<float> Radii;
            Radii.reserve(Samples.size());
            for (const FBoneVertexSample& Sample : Samples)
            {
                const float Projection = Sample.LocalPosition.Dot(CapsuleAxis);
                if (Projection < MinProjection || Projection > MaxProjection)
                {
                    continue;
                }
                const FVector AxisPoint = PerpCenter + CapsuleAxis * Projection;
                Radii.push_back((Sample.LocalPosition - AxisPoint).Length());
            }

            const float Radius = std::max(Percentile(Radii, UpperPercentile) * ShapePadding, MinShapeSize);
            const float ProjectionSpan = std::max(MaxProjection - MinProjection, MinShapeSize);

            FKSphylElem Sphyl;
            Sphyl.Name = Bone.Name + " Auto Capsule";
            Sphyl.Radius = Radius;
            Sphyl.Length = std::max(ProjectionSpan - Radius * 2.0f, MinShapeSize);
            Sphyl.Transform.Location = PerpCenter + CapsuleAxis * CenterProjection;
            Sphyl.Transform.Rotation = MakeQuatFromZAxis(CapsuleAxis);
            BodySetup->GetAggGeom().SphylElems.push_back(Sphyl);

            UE_LOG("[PhysicsAssetAutoGen] Body bone=%s shape=%s samples=%d center=(%.3f,%.3f,%.3f) radius=%.3f length=%.3f axis=(%.3f,%.3f,%.3f)",
                Bone.Name.c_str(),
                ToShapeLogName(Shape),
                static_cast<int32>(Samples.size()),
                Sphyl.Transform.Location.X,
                Sphyl.Transform.Location.Y,
                Sphyl.Transform.Location.Z,
                Sphyl.Radius,
                Sphyl.Length,
                CapsuleAxis.X,
                CapsuleAxis.Y,
                CapsuleAxis.Z);
        }
        else
        {
            FKBoxElem Box;
            Box.Name = Bone.Name + " Auto Box";
            Box.Extent = Extent;
            Box.Transform.Location = Center;
            BodySetup->GetAggGeom().BoxElems.push_back(Box);

            UE_LOG("[PhysicsAssetAutoGen] Body bone=%s shape=Box samples=%d center=(%.3f,%.3f,%.3f) extent=(%.3f,%.3f,%.3f)",
                Bone.Name.c_str(),
                static_cast<int32>(Samples.size()),
                Box.Transform.Location.X,
                Box.Transform.Location.Y,
                Box.Transform.Location.Z,
                Box.Extent.X,
                Box.Extent.Y,
                Box.Extent.Z);
        }

        if (!BodySetup->HasGeometry())
        {
            UE_LOG("[PhysicsAssetAutoGen] Skip bone=%s reason=NoGeometry shape=%s",
                Bone.Name.c_str(),
                ToShapeLogName(Shape));
            UObjectManager::Get().DestroyObject(BodySetup);
            ++Stats.SkippedBoneCount;
            continue;
        }

        BodySetups.push_back(BodySetup);
        bHasBodyForBone[BoneIndex] = true;
        ++Stats.BodyCount;
    }

    if (Settings.bCreateConstraints)
    {
        for (int32 ChildBoneIndex = 0; ChildBoneIndex < BoneCount; ++ChildBoneIndex)
        {
            if (!bHasBodyForBone[ChildBoneIndex])
            {
                continue;
            }

            int32 ParentBoneIndex = Mesh.Bones[ChildBoneIndex].ParentIndex;
            while (ParentBoneIndex >= 0 && ParentBoneIndex < BoneCount && !bHasBodyForBone[ParentBoneIndex])
            {
                ParentBoneIndex = Mesh.Bones[ParentBoneIndex].ParentIndex;
            }

            if (ParentBoneIndex < 0 || ParentBoneIndex >= BoneCount)
            {
                continue;
            }

            const FName ParentBoneName(Mesh.Bones[ParentBoneIndex].Name);
            const FName ChildBoneName(Mesh.Bones[ChildBoneIndex].Name);
            if (HasConstraint(ConstraintSetups, ParentBoneName, ChildBoneName))
            {
                continue;
            }

            FConstraintSetup Constraint;
            Constraint.ConstraintName = Mesh.Bones[ParentBoneIndex].Name + " -> " + Mesh.Bones[ChildBoneIndex].Name;
            Constraint.ParentBoneName = ParentBoneName;
            Constraint.ChildBoneName = ChildBoneName;
            Constraint.ParentFrame = FTransform(
                Mesh.Bones[ChildBoneIndex].GetReferenceGlobalPose()
                * Mesh.Bones[ParentBoneIndex].GetReferenceGlobalPose().GetInverse());
            Constraint.ChildFrame = FTransform();
            Constraint.Option.TwistLimitDegrees = 60.0f;
            Constraint.Option.Swing1LimitDegrees = 60.0f;
            Constraint.Option.Swing2LimitDegrees = 60.0f;
            ConstraintSetups.push_back(Constraint);
            ++Stats.ConstraintCount;

            UE_LOG("[PhysicsAssetAutoGen] Constraint parent=%s child=%s",
                Mesh.Bones[ParentBoneIndex].Name.c_str(),
                Mesh.Bones[ChildBoneIndex].Name.c_str());
        }
    }

    if (OutStats)
    {
        *OutStats = Stats;
    }

    UE_LOG("[PhysicsAssetAutoGen] Completed: bodies=%d constraints=%d skipped=%d",
        Stats.BodyCount,
        Stats.ConstraintCount,
        Stats.SkippedBoneCount);

    return Stats.BodyCount > 0;
}
