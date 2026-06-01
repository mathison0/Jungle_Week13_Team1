#include "BoneDebugSceneProxy.h"

#include "Component/Debug/BoneDebugComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"

#include <algorithm>

#pragma region Line Draw

static void AddLine(TArray<FWireLine>& Lines, const FVector& A, const FVector& B)
{
	Lines.push_back({ A, B });
}

static void AddWireCircle(TArray<FWireLine>& Lines, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	float Radius, int32 Segments)
{
	if (Radius <= 0.0f || Segments < 3) return;

	const float Step = 2.0f * FMath::Pi / static_cast<float>(Segments);
	FVector Prev = Center + AxisA * Radius;

	for (int32 i = 1; i <= Segments; ++i)
	{
		const float Angle = Step * i;
		FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
		AddLine(Lines, Prev, Next);
		Prev = Next;
	}
}

static void AddWireHalfCircle(TArray<FWireLine>& Lines, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	float Radius, int32 Segments, float StartAngle)
{
	if (Radius <= 0.0f || Segments < 3) return;

	const float Step = FMath::Pi / static_cast<float>(Segments);
	FVector Prev = Center + (AxisA * cosf(StartAngle) + AxisB * sinf(StartAngle)) * Radius;

	for (int32 i = 1; i <= Segments; ++i)
	{
		const float Angle = StartAngle + Step * static_cast<float>(i);
		FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
		AddLine(Lines, Prev, Next);
		Prev = Next;
	}
}

static void BuildLowSphereLines(TArray<FWireLine>& Lines, const FVector& Center, float Radius)
{
	constexpr int32 Segments = 8;

	const FVector AxisA(1.0f, 0.0f, 0.0f);
	const FVector AxisB(0.0f, 1.0f, 0.0f);
	const FVector AxisC(0.0f, 0.0f, 1.0f);
	AddWireCircle(Lines, Center, AxisA, AxisB, Radius, 12);
	AddWireCircle(Lines, Center, AxisB, AxisC, Radius, 12);
	AddWireCircle(Lines, Center, AxisC, AxisA, Radius, 12);
}

static void BuildPhysicsSphereLines(TArray<FWireLine>& Lines, const FVector& Center, float Radius)
{
	constexpr int32 Segments = 24;
	AddWireCircle(Lines, Center, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), Radius, Segments);
	AddWireCircle(Lines, Center, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), Radius, Segments);
	AddWireCircle(Lines, Center, FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), Radius, Segments);
}

static void BuildPhysicsBoxLines(TArray<FWireLine>& Lines, const FVector& Center, const FVector& Extent,
	const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ)
{
	FVector Corners[8];
	for (int32 i = 0; i < 8; ++i)
	{
		const float X = (i & 1) ? Extent.X : -Extent.X;
		const float Y = (i & 2) ? Extent.Y : -Extent.Y;
		const float Z = (i & 4) ? Extent.Z : -Extent.Z;
		Corners[i] = Center + AxisX * X + AxisY * Y + AxisZ * Z;
	}

	AddLine(Lines, Corners[0], Corners[1]);
	AddLine(Lines, Corners[1], Corners[3]);
	AddLine(Lines, Corners[3], Corners[2]);
	AddLine(Lines, Corners[2], Corners[0]);
	AddLine(Lines, Corners[4], Corners[5]);
	AddLine(Lines, Corners[5], Corners[7]);
	AddLine(Lines, Corners[7], Corners[6]);
	AddLine(Lines, Corners[6], Corners[4]);
	AddLine(Lines, Corners[0], Corners[4]);
	AddLine(Lines, Corners[1], Corners[5]);
	AddLine(Lines, Corners[2], Corners[6]);
	AddLine(Lines, Corners[3], Corners[7]);
}

static void BuildPhysicsCapsuleLines(TArray<FWireLine>& Lines, const FVector& Center, float Radius, float HalfHeight,
	const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ)
{
	if (Radius <= 0.0f || HalfHeight <= 0.0f) return;

	const float CylinderHalf = std::max(0.0f, HalfHeight - Radius);
	constexpr int32 Segments = 24;
	constexpr int32 HalfSegments = 12;

	const FVector TopCenter = Center + AxisZ * CylinderHalf;
	const FVector BotCenter = Center - AxisZ * CylinderHalf;

	AddWireCircle(Lines, TopCenter, AxisX, AxisY, Radius, Segments);
	AddWireCircle(Lines, BotCenter, AxisX, AxisY, Radius, Segments);

	AddLine(Lines, TopCenter + AxisX * Radius, BotCenter + AxisX * Radius);
	AddLine(Lines, TopCenter - AxisX * Radius, BotCenter - AxisX * Radius);
	AddLine(Lines, TopCenter + AxisY * Radius, BotCenter + AxisY * Radius);
	AddLine(Lines, TopCenter - AxisY * Radius, BotCenter - AxisY * Radius);

	AddWireHalfCircle(Lines, TopCenter, AxisX, AxisZ, Radius, HalfSegments, 0.0f);
	AddWireHalfCircle(Lines, TopCenter, AxisY, AxisZ, Radius, HalfSegments, 0.0f);
	AddWireHalfCircle(Lines, BotCenter, AxisX, AxisZ, Radius, HalfSegments, FMath::Pi);
	AddWireHalfCircle(Lines, BotCenter, AxisY, AxisZ, Radius, HalfSegments, FMath::Pi);
}

static void BuildBonePyramidLines(TArray<FWireLine>& Lines, const FVector& Start, const FVector& End, float WidthScale)
{
	FVector BoneVector = End - Start;
	const float Length = BoneVector.Length();

	if (Length <= 0.001f) return;

	const FVector Dir = BoneVector / Length;

	FVector UpHint = std::fabs(Dir.Z) > 0.9f ? FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 0.0f, 1.0f);
	FVector Right = UpHint.Cross(Dir).Normalized();
	FVector Up = Dir.Cross(Right).Normalized();

	const float HalfWidth = Length * WidthScale;

	const FVector Center = End;

	const FVector C0 = Center + Right * HalfWidth + Up * HalfWidth;
	const FVector C1 = Center - Right * HalfWidth + Up * HalfWidth;
	const FVector C2 = Center - Right * HalfWidth - Up * HalfWidth;
	const FVector C3 = Center + Right * HalfWidth - Up * HalfWidth;

	AddLine(Lines, Start, C0);
	AddLine(Lines, Start, C1);
	AddLine(Lines, Start, C2);
	AddLine(Lines, Start, C3);

	AddLine(Lines, C0, C1);
	AddLine(Lines, C1, C2);
	AddLine(Lines, C2, C3);
	AddLine(Lines, C3, C0);
}

static float NormalizeAxis(FVector& Axis)
{
	const float Length = Axis.Length();
	if (Length > 0.0001f)
	{
		Axis = Axis / Length;
		return Length;
	}

	Axis = FVector(1.0f, 0.0f, 0.0f);
	return 1.0f;
}

static void ExtractTransformAxes(const FMatrix& Matrix, FVector& OutCenter, FVector& OutAxisX, FVector& OutAxisY, FVector& OutAxisZ, FVector& OutScale)
{
	OutCenter = Matrix.GetLocation();
	OutAxisX = FVector(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]);
	OutAxisY = FVector(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]);
	OutAxisZ = FVector(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]);
	OutScale.X = NormalizeAxis(OutAxisX);
	OutScale.Y = NormalizeAxis(OutAxisY);
	OutScale.Z = NormalizeAxis(OutAxisZ);
}

#pragma endregion


FBoneDebugSceneProxy::FBoneDebugSceneProxy(UBoneDebugComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags = EPrimitiveProxyFlags::EditorOnly
		| EPrimitiveProxyFlags::NeverCull
		| EPrimitiveProxyFlags::PerViewportUpdate
		| EPrimitiveProxyFlags::BoneDebug;

	BoneColor = FVector4(0.49f, 0.91f, 0.48f, 1.0f);
	ParentBoneColor = FVector4(0.93f, 0.69f, 0.38f, 1.0f);
	PhysicsAssetColor = FVector4(0.18f, 0.62f, 1.0f, 0.5f);
	RebuildLines();
}

FBoneDebugSceneProxy::~FBoneDebugSceneProxy()
{
}

void FBoneDebugSceneProxy::UpdateTransform()
{
	FPrimitiveSceneProxy::UpdateTransform();
	RebuildLines();
}

void FBoneDebugSceneProxy::UpdatePerViewport(const FFrameContext& /*Frame*/)
{
	RebuildLines();
}

void FBoneDebugSceneProxy::RebuildLines()
{
	CachedLines.clear();
	CachedParentBoneLines.clear();
	CachedPhysicsAssetLines.clear();

	UBoneDebugComponent* Comp = static_cast<UBoneDebugComponent*>(GetOwner());
	if (!Comp) return;

	USkeletalMeshComponent* MeshComp = Comp->GetTargetMeshComponent();
	if (!MeshComp) return;

	USkeletalMesh* Mesh = MeshComp->GetSkeletalMesh();
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset) return;

	RebuildPhysicsAssetLines(Comp, MeshComp, Asset);

	const int32 BoneCount = static_cast<int32>(Asset->Bones.size());
	if (BoneCount <= 0) return;

	const FBoundingBox Bounds = MeshComp->GetWorldBoundingBox();
	const FVector Extent = Bounds.GetExtent();
	const float ModelSize = Extent.Length();

	const float JointRadius = ModelSize * 0.01f;
	const float PyramidWidthScale = 0.03f;

	if (Comp->GetDrawMode() == EBoneDebugDrawMode::AllBones)
	{
		for (int32 i = 0; i < BoneCount; ++i)
		{
			const FVector BonePos = MeshComp->GetBoneLocationByIndex(i);
			BuildLowSphereLines(CachedLines, BonePos, JointRadius);

			const int32 ParentIndex = Asset->Bones[i].ParentIndex;
			if (ParentIndex >= 0 && ParentIndex < BoneCount)
			{
				const FVector ParentPos = MeshComp->GetBoneLocationByIndex(ParentIndex);
				BuildBonePyramidLines(CachedLines, BonePos, ParentPos, PyramidWidthScale);
			}
		}
		return;
	}

	const int32 BoneIndex = Comp->GetSelectedBoneIndex();
	if (BoneIndex < 0 || BoneIndex >= BoneCount) return;

	const FVector BonePos = MeshComp->GetBoneLocationByIndex(BoneIndex);

	BuildLowSphereLines(CachedLines, BonePos, JointRadius);

	for (int32 i = 0; i < BoneCount; ++i)
	{
		if (Asset->Bones[i].ParentIndex == BoneIndex)
		{
			const FVector ChildPos = MeshComp->GetBoneLocationByIndex(i);
			BuildBonePyramidLines(CachedLines, ChildPos, BonePos, PyramidWidthScale);
		}
	}

	if (Asset->Bones[BoneIndex].ParentIndex != -1)
	{
		const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
		const FVector ParentPos = MeshComp->GetBoneLocationByIndex(ParentIndex);
		BuildBonePyramidLines(CachedParentBoneLines, BonePos, ParentPos, PyramidWidthScale);
	}
}

void FBoneDebugSceneProxy::RebuildPhysicsAssetLines(UBoneDebugComponent* Comp, USkeletalMeshComponent* MeshComp, const FSkeletalMesh* Asset)
{
	if (!Comp || !Comp->ShouldDrawPhysicsAsset() || !MeshComp || !Asset) return;

	USkeletalMesh* Mesh = MeshComp->GetSkeletalMesh();
	UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!PhysicsAsset) return;

	for (UBodySetup* BodySetup : PhysicsAsset->BodySetups)
	{
		if (!BodySetup || !BodySetup->HasGeometry()) continue;

		const FString BoneName = BodySetup->GetBoneName().ToString();
		const int32 BoneIndex = MeshComp->FindBoneIndex(BoneName);
		if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size())) continue;

		FTransform BoneWorldTransform;
		if (!MeshComp->GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform)) continue;

		const FMatrix BoneWorldMatrix = BoneWorldTransform.ToMatrix();
		const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();

		for (const FKSphereElem& Sphere : AggGeom.SphereElems)
		{
			const FMatrix ShapeWorldMatrix = Sphere.Transform.ToMatrix() * BoneWorldMatrix;
			FVector Center, AxisX, AxisY, AxisZ, Scale;
			ExtractTransformAxes(ShapeWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);
			const float Radius = Sphere.Radius * std::max({ Scale.X, Scale.Y, Scale.Z });
			BuildPhysicsSphereLines(CachedPhysicsAssetLines, Center, Radius);
		}

		for (const FKBoxElem& Box : AggGeom.BoxElems)
		{
			const FMatrix ShapeWorldMatrix = Box.Transform.ToMatrix() * BoneWorldMatrix;
			FVector Center, AxisX, AxisY, AxisZ, Scale;
			ExtractTransformAxes(ShapeWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);
			const FVector Extent(Box.Extent.X * Scale.X, Box.Extent.Y * Scale.Y, Box.Extent.Z * Scale.Z);
			BuildPhysicsBoxLines(CachedPhysicsAssetLines, Center, Extent, AxisX, AxisY, AxisZ);
		}

		for (const FKSphylElem& Sphyl : AggGeom.SphylElems)
		{
			const FMatrix ShapeWorldMatrix = Sphyl.Transform.ToMatrix() * BoneWorldMatrix;
			FVector Center, AxisX, AxisY, AxisZ, Scale;
			ExtractTransformAxes(ShapeWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);
			const float Radius = Sphyl.Radius * std::max(Scale.X, Scale.Y);
			const float HalfHeight = Sphyl.Length * 0.5f * Scale.Z + Radius;
			BuildPhysicsCapsuleLines(CachedPhysicsAssetLines, Center, Radius, HalfHeight, AxisX, AxisY, AxisZ);
		}
	}
}
