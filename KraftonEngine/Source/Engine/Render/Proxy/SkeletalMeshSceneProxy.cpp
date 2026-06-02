#include "SkeletalMeshSceneProxy.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Types/FrameContext.h"
#include "Runtime/Engine.h"
#include "Profiling/Time/Timer.h"
#include "Profiling/Stats/Stats.h"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace
{
void AddPhysicsDebugLine(TArray<FWireLine>& Lines, const FVector& A, const FVector& B)
{
	Lines.push_back({ A, B });
}

void AddWireCircle(TArray<FWireLine>& Lines, const FVector& Center, const FVector& AxisA, const FVector& AxisB, float Radius, int32 Segments)
{
	if (Radius <= 0.0f || Segments < 3) return;

	const float Step = 2.0f * FMath::Pi / static_cast<float>(Segments);
	FVector Prev = Center + AxisA * Radius;
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float Angle = Step * static_cast<float>(i);
		const FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
		AddPhysicsDebugLine(Lines, Prev, Next);
		Prev = Next;
	}
}

void AddWireHalfCircle(TArray<FWireLine>& Lines, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	float Radius, int32 Segments, float StartAngle)
{
	if (Radius <= 0.0f || Segments < 3) return;

	const float Step = FMath::Pi / static_cast<float>(Segments);
	FVector Prev = Center + (AxisA * cosf(StartAngle) + AxisB * sinf(StartAngle)) * Radius;
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float Angle = StartAngle + Step * static_cast<float>(i);
		const FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
		AddPhysicsDebugLine(Lines, Prev, Next);
		Prev = Next;
	}
}

void BuildPhysicsSphereLines(TArray<FWireLine>& Lines, const FVector& Center, float Radius)
{
	constexpr int32 Segments = 24;
	AddWireCircle(Lines, Center, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), Radius, Segments);
	AddWireCircle(Lines, Center, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), Radius, Segments);
	AddWireCircle(Lines, Center, FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), Radius, Segments);
}

void BuildPhysicsBoxLines(TArray<FWireLine>& Lines, const FVector& Center, const FVector& Extent,
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

	AddPhysicsDebugLine(Lines, Corners[0], Corners[1]); AddPhysicsDebugLine(Lines, Corners[1], Corners[3]);
	AddPhysicsDebugLine(Lines, Corners[3], Corners[2]); AddPhysicsDebugLine(Lines, Corners[2], Corners[0]);
	AddPhysicsDebugLine(Lines, Corners[4], Corners[5]); AddPhysicsDebugLine(Lines, Corners[5], Corners[7]);
	AddPhysicsDebugLine(Lines, Corners[7], Corners[6]); AddPhysicsDebugLine(Lines, Corners[6], Corners[4]);
	AddPhysicsDebugLine(Lines, Corners[0], Corners[4]); AddPhysicsDebugLine(Lines, Corners[1], Corners[5]);
	AddPhysicsDebugLine(Lines, Corners[2], Corners[6]); AddPhysicsDebugLine(Lines, Corners[3], Corners[7]);
}

void BuildPhysicsCapsuleLines(TArray<FWireLine>& Lines, const FVector& Center, float Radius, float HalfHeight,
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
	AddPhysicsDebugLine(Lines, TopCenter + AxisX * Radius, BotCenter + AxisX * Radius);
	AddPhysicsDebugLine(Lines, TopCenter - AxisX * Radius, BotCenter - AxisX * Radius);
	AddPhysicsDebugLine(Lines, TopCenter + AxisY * Radius, BotCenter + AxisY * Radius);
	AddPhysicsDebugLine(Lines, TopCenter - AxisY * Radius, BotCenter - AxisY * Radius);
	AddWireHalfCircle(Lines, TopCenter, AxisX, AxisZ, Radius, HalfSegments, 0.0f);
	AddWireHalfCircle(Lines, TopCenter, AxisY, AxisZ, Radius, HalfSegments, 0.0f);
	AddWireHalfCircle(Lines, BotCenter, AxisX, AxisZ, Radius, HalfSegments, FMath::Pi);
	AddWireHalfCircle(Lines, BotCenter, AxisY, AxisZ, Radius, HalfSegments, FMath::Pi);
}

void AddSolidVertex(TArray<FVertex>& Vertices, const FVector& Position, const FVector4& Color)
{
	Vertices.push_back({ Position, Color, 0 });
}

void AddSolidTriangle(TArray<FVertex>& Vertices, TArray<uint32>& Indices, const FVector& A, const FVector& B, const FVector& C, const FVector4& Color)
{
	const uint32 BaseIndex = static_cast<uint32>(Vertices.size());
	AddSolidVertex(Vertices, A, Color);
	AddSolidVertex(Vertices, B, Color);
	AddSolidVertex(Vertices, C, Color);
	Indices.push_back(BaseIndex);
	Indices.push_back(BaseIndex + 1);
	Indices.push_back(BaseIndex + 2);
}

void BuildPhysicsBoxSolid(TArray<FVertex>& Vertices, TArray<uint32>& Indices, const FVector& Center, const FVector& Extent,
	const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ, const FVector4& Color)
{
	FVector Corners[8];
	for (int32 i = 0; i < 8; ++i)
	{
		const float X = (i & 1) ? Extent.X : -Extent.X;
		const float Y = (i & 2) ? Extent.Y : -Extent.Y;
		const float Z = (i & 4) ? Extent.Z : -Extent.Z;
		Corners[i] = Center + AxisX * X + AxisY * Y + AxisZ * Z;
	}

	static constexpr int32 FaceTris[][3] =
	{
		{0, 1, 3}, {0, 3, 2}, {4, 7, 5}, {4, 6, 7},
		{0, 4, 1}, {1, 4, 5}, {2, 3, 6}, {3, 7, 6},
		{0, 2, 4}, {2, 6, 4}, {1, 5, 3}, {3, 5, 7}
	};

	for (const auto& Tri : FaceTris)
	{
		AddSolidTriangle(Vertices, Indices, Corners[Tri[0]], Corners[Tri[1]], Corners[Tri[2]], Color);
	}
}

FVector MakeEllipsoidPoint(const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
	float RadiusX, float RadiusY, float RadiusZ, float Theta, float Phi)
{
	const float SinPhi = sinf(Phi);
	return Center
		+ AxisX * (RadiusX * SinPhi * cosf(Theta))
		+ AxisY * (RadiusY * SinPhi * sinf(Theta))
		+ AxisZ * (RadiusZ * cosf(Phi));
}

void BuildPhysicsSphereSolid(TArray<FVertex>& Vertices, TArray<uint32>& Indices, const FVector& Center,
	float RadiusX, float RadiusY, float RadiusZ, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ, const FVector4& Color)
{
	constexpr int32 Slices = 16;
	constexpr int32 Stacks = 8;

	for (int32 Stack = 0; Stack < Stacks; ++Stack)
	{
		const float Phi0 = FMath::Pi * static_cast<float>(Stack) / static_cast<float>(Stacks);
		const float Phi1 = FMath::Pi * static_cast<float>(Stack + 1) / static_cast<float>(Stacks);

		for (int32 Slice = 0; Slice < Slices; ++Slice)
		{
			const float Theta0 = 2.0f * FMath::Pi * static_cast<float>(Slice) / static_cast<float>(Slices);
			const float Theta1 = 2.0f * FMath::Pi * static_cast<float>(Slice + 1) / static_cast<float>(Slices);
			const FVector P00 = MakeEllipsoidPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, Theta0, Phi0);
			const FVector P01 = MakeEllipsoidPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, Theta1, Phi0);
			const FVector P10 = MakeEllipsoidPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, Theta0, Phi1);
			const FVector P11 = MakeEllipsoidPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, Theta1, Phi1);

			if (Stack > 0)
			{
				AddSolidTriangle(Vertices, Indices, P00, P10, P01, Color);
			}
			if (Stack + 1 < Stacks)
			{
				AddSolidTriangle(Vertices, Indices, P01, P10, P11, Color);
			}
		}
	}
}

FVector MakeCapsuleRingPoint(const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
	float RadiusX, float RadiusY, float RadiusZ, float CylinderHalf, bool bTop, float Theta, float Phi)
{
	const float Sign = bTop ? 1.0f : -1.0f;
	const float SinPhi = sinf(Phi);
	const float LocalZ = Sign * (CylinderHalf + RadiusZ * cosf(Phi));
	return Center
		+ AxisX * (RadiusX * SinPhi * cosf(Theta))
		+ AxisY * (RadiusY * SinPhi * sinf(Theta))
		+ AxisZ * LocalZ;
}

void BuildPhysicsCapsuleSolid(TArray<FVertex>& Vertices, TArray<uint32>& Indices, const FVector& Center,
	float RadiusX, float RadiusY, float RadiusZ, float CylinderHalf,
	const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ, const FVector4& Color)
{
	constexpr int32 Slices = 16;
	constexpr int32 CapStacks = 4;
	for (int32 Slice = 0; Slice < Slices; ++Slice)
	{
		const float Theta0 = 2.0f * FMath::Pi * static_cast<float>(Slice) / static_cast<float>(Slices);
		const float Theta1 = 2.0f * FMath::Pi * static_cast<float>(Slice + 1) / static_cast<float>(Slices);
		const FVector Top0 = Center + AxisX * (RadiusX * cosf(Theta0)) + AxisY * (RadiusY * sinf(Theta0)) + AxisZ * CylinderHalf;
		const FVector Top1 = Center + AxisX * (RadiusX * cosf(Theta1)) + AxisY * (RadiusY * sinf(Theta1)) + AxisZ * CylinderHalf;
		const FVector Bot0 = Center + AxisX * (RadiusX * cosf(Theta0)) + AxisY * (RadiusY * sinf(Theta0)) - AxisZ * CylinderHalf;
		const FVector Bot1 = Center + AxisX * (RadiusX * cosf(Theta1)) + AxisY * (RadiusY * sinf(Theta1)) - AxisZ * CylinderHalf;
		AddSolidTriangle(Vertices, Indices, Top0, Bot0, Top1, Color);
		AddSolidTriangle(Vertices, Indices, Top1, Bot0, Bot1, Color);

		for (int32 Stack = 0; Stack < CapStacks; ++Stack)
		{
			const float Phi0 = (FMath::Pi * 0.5f) * static_cast<float>(Stack) / static_cast<float>(CapStacks);
			const float Phi1 = (FMath::Pi * 0.5f) * static_cast<float>(Stack + 1) / static_cast<float>(CapStacks);
			const FVector T00 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, true, Theta0, Phi0);
			const FVector T01 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, true, Theta1, Phi0);
			const FVector T10 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, true, Theta0, Phi1);
			const FVector T11 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, true, Theta1, Phi1);
			AddSolidTriangle(Vertices, Indices, T00, T10, T01, Color);
			AddSolidTriangle(Vertices, Indices, T01, T10, T11, Color);

			const FVector B00 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, false, Theta0, Phi0);
			const FVector B01 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, false, Theta1, Phi0);
			const FVector B10 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, false, Theta0, Phi1);
			const FVector B11 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, false, Theta1, Phi1);
			AddSolidTriangle(Vertices, Indices, B00, B01, B10, Color);
			AddSolidTriangle(Vertices, Indices, B01, B11, B10, Color);
		}
	}
}

void BuildConstraintSwingCone(TArray<FVertex>& Vertices, TArray<uint32>& Indices,
	const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
	float ConeLength, float Swing1LimitDegrees, float Swing2LimitDegrees, const FVector4& ConeColor, const FVector4& ArcColor)
{
	if (ConeLength <= 0.0f || (Swing1LimitDegrees <= 0.0f && Swing2LimitDegrees <= 0.0f)) return;

	const float Swing1Radians = FMath::Clamp(Swing1LimitDegrees, 0.0f, 89.0f) * FMath::Pi / 180.0f;
	const float Swing2Radians = FMath::Clamp(Swing2LimitDegrees, 0.0f, 89.0f) * FMath::Pi / 180.0f;
	const float Swing1Tan = tanf(Swing1Radians);
	const float Swing2Tan = tanf(Swing2Radians);
	const float ArcWidth = std::max(0.004f, ConeLength * 0.035f);
	constexpr int32 Segments = 36;

	FVector TwistAxis = AxisX;
	FVector Swing2Axis = AxisY;
	FVector Swing1Axis = AxisZ;
	TwistAxis.Normalize();
	Swing2Axis.Normalize();
	Swing1Axis.Normalize();

	auto MakeConePoint = [&](float Angle, float Length)
	{
		FVector Direction = TwistAxis
			+ Swing2Axis * (cosf(Angle) * Swing2Tan)
			+ Swing1Axis * (sinf(Angle) * Swing1Tan);
		if (Direction.IsNearlyZero())
		{
			Direction = TwistAxis;
		}
		Direction.Normalize();
		return Center + Direction * Length;
	};

	FVector PrevInner = MakeConePoint(0.0f, ConeLength);
	FVector PrevOuter = MakeConePoint(0.0f, ConeLength + ArcWidth);
	for (int32 Segment = 1; Segment <= Segments; ++Segment)
	{
		const float Angle = 2.0f * FMath::Pi * static_cast<float>(Segment) / static_cast<float>(Segments);
		const FVector NextInner = MakeConePoint(Angle, ConeLength);
		const FVector NextOuter = MakeConePoint(Angle, ConeLength + ArcWidth);
		AddSolidTriangle(Vertices, Indices, Center, PrevInner, NextInner, ConeColor);
		AddSolidTriangle(Vertices, Indices, PrevInner, PrevOuter, NextInner, ArcColor);
		AddSolidTriangle(Vertices, Indices, NextInner, PrevOuter, NextOuter, ArcColor);
		PrevInner = NextInner;
		PrevOuter = NextOuter;
	}
}

void BuildConstraintSolidSector(TArray<FVertex>& Vertices, TArray<uint32>& Indices,
	const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	float Radius, float LimitDegrees, const FVector4& Color)
{
	if (Radius <= 0.0f || LimitDegrees <= 0.0f) return;

	const float LimitRadians = FMath::Clamp(LimitDegrees, 0.0f, 180.0f) * FMath::Pi / 180.0f;
	constexpr int32 Segments = 32;
	FVector Prev = Center + (AxisA * cosf(-LimitRadians) + AxisB * sinf(-LimitRadians)) * Radius;

	for (int32 Segment = 1; Segment <= Segments; ++Segment)
	{
		const float Alpha = static_cast<float>(Segment) / static_cast<float>(Segments);
		const float Angle = -LimitRadians + LimitRadians * 2.0f * Alpha;
		const FVector Next = Center + (AxisA * cosf(Angle) + AxisB * sinf(Angle)) * Radius;
		AddSolidTriangle(Vertices, Indices, Center, Prev, Next, Color);
		Prev = Next;
	}
}

float GetConstraintVisualLimitDegrees(EAngularConstraintMotion Motion, float LimitDegrees, float FreeDegrees)
{
	switch (Motion)
	{
	case EAngularConstraintMotion::Free:
		return FreeDegrees;
	case EAngularConstraintMotion::Limited:
		return FMath::Clamp(LimitDegrees, 0.0f, FreeDegrees);
	case EAngularConstraintMotion::Locked:
	default:
		return 0.0f;
	}
}

float NormalizeAxis(FVector& Axis)
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

void ExtractTransformAxes(const FMatrix& Matrix, FVector& OutCenter, FVector& OutAxisX, FVector& OutAxisY, FVector& OutAxisZ, FVector& OutScale)
{
	OutCenter = Matrix.GetLocation();
	OutAxisX = FVector(Matrix.M[0][0], Matrix.M[0][1], Matrix.M[0][2]);
	OutAxisY = FVector(Matrix.M[1][0], Matrix.M[1][1], Matrix.M[1][2]);
	OutAxisZ = FVector(Matrix.M[2][0], Matrix.M[2][1], Matrix.M[2][2]);
	OutScale.X = NormalizeAxis(OutAxisX);
	OutScale.Y = NormalizeAxis(OutAxisY);
	OutScale.Z = NormalizeAxis(OutAxisZ);
}

FMatrix BuildConstraintDisplayWorldMatrix(const FConstraintSetup& Constraint, const FTransform& ParentBoneWorldTransform, const FTransform& ChildBoneWorldTransform)
{
	const FTransform ParentFrameWorld(Constraint.ParentFrame.ToMatrix() * ParentBoneWorldTransform.ToMatrix());
	const FTransform ChildFrameWorld(Constraint.ChildFrame.ToMatrix() * ChildBoneWorldTransform.ToMatrix());
	const FTransform DisplayTransform(
		FVector::Lerp(ParentFrameWorld.Location, ChildFrameWorld.Location, 0.5f),
		FQuat::Slerp(ParentFrameWorld.Rotation.GetNormalized(), ChildFrameWorld.Rotation.GetNormalized(), 0.5f).GetNormalized(),
		FVector::Lerp(ParentFrameWorld.Scale, ChildFrameWorld.Scale, 0.5f));
	return DisplayTransform.ToMatrix();
}
}

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(USkeletalMeshComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	ProxyFlags |= EPrimitiveProxyFlags::SkeletalMesh | EPrimitiveProxyFlags::PerViewportUpdate;
}

FSkeletalMeshSceneProxy::~FSkeletalMeshSceneProxy()
{
	ReleaseSkinMatrixBuffer();
}   

USkeletalMeshComponent* FSkeletalMeshSceneProxy::GetSkeletalMeshComponent() const
{
	return static_cast<USkeletalMeshComponent*>(GetOwner());
}

void FSkeletalMeshSceneProxy::UpdateMaterial()
{
	RebuildSectionDraws();
};

void FSkeletalMeshSceneProxy::UpdateMesh()
{
	MeshBuffer = GetOwner()->GetMeshBuffer();
	RebuildSectionDraws();
	CachedPhysicsAssetLines.clear();
	CachedPhysicsAssetSolidVertices.clear();
	CachedPhysicsAssetSolidIndices.clear();
	CachedPhysicsConstraintSolidVertices.clear();
	CachedPhysicsConstraintSolidIndices.clear();

	CachedDynamicVertexCount = 0;
	UploadedSkinnedRevision = 0;
	UploadedSkinMatrixRevision = 0;
	bDynamicBufferNeedsCreate = true;
	ReleaseSkinMatrixBuffer();

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (Asset)
	{
		CachedDynamicVertexCount = static_cast<uint32>(Asset->Vertices.size());
	}
}

void FSkeletalMeshSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	// 메인 viewport의 PhysicsAsset show flag는 SkeletalMesh 프록시에서 직접 처리합니다.
	RebuildPhysicsAssetDebugGeometry(Frame);
}

void FSkeletalMeshSceneProxy::RebuildPhysicsAssetDebugGeometry(const FFrameContext& Frame)
{
	CachedPhysicsAssetLines.clear();
	CachedPhysicsAssetSolidVertices.clear();
	CachedPhysicsAssetSolidIndices.clear();
	CachedPhysicsConstraintSolidVertices.clear();
	CachedPhysicsConstraintSolidIndices.clear();

	if (Frame.WorldType == EWorldType::EditorPreview)
	{
		// Mesh editor preview는 UBoneDebugComponent가 PhysicsAsset을 따로 그립니다.
		return;
	}

	const EPhysicsAssetBodyShowMode BodyShowMode = Frame.RenderOptions.PhysicsAssetBodyShowMode;
	const EPhysicsAssetConstraintShowMode ConstraintShowMode = Frame.RenderOptions.PhysicsAssetConstraintShowMode;
	const bool bDrawBodyWireframe = BodyShowMode == EPhysicsAssetBodyShowMode::Wireframe;
	const bool bDrawBodySolid = BodyShowMode == EPhysicsAssetBodyShowMode::Solid;
	const bool bDrawConstraintSolid = ConstraintShowMode == EPhysicsAssetConstraintShowMode::Solid;
	if (!bDrawBodyWireframe && !bDrawBodySolid && !bDrawConstraintSolid)
	{
		return;
	}

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!SMC || !Asset || !PhysicsAsset)
	{
		return;
	}

	const FVector4 BodySolidColor(0.56f, 0.58f, 0.60f, 0.30f);

	// Body shape은 bone world transform과 shape local transform을 합쳐서 그립니다.
	for (UBodySetup* BodySetup : PhysicsAsset->BodySetups)
	{
		if (!BodySetup || !BodySetup->HasGeometry()) continue;

		const int32 BoneIndex = SMC->FindBoneIndex(BodySetup->GetBoneName().ToString());
		if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size())) continue;

		FTransform BoneWorldTransform;
		if (!SMC->GetBoneWorldTransformByIndex(BoneIndex, BoneWorldTransform)) continue;

		const FMatrix BoneWorldMatrix = BoneWorldTransform.ToMatrix();
		const FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();

		for (const FKSphereElem& Sphere : AggGeom.SphereElems)
		{
			const FMatrix ShapeWorldMatrix = Sphere.Transform.ToMatrix() * BoneWorldMatrix;
			FVector Center, AxisX, AxisY, AxisZ, Scale;
			ExtractTransformAxes(ShapeWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);
			const float Radius = Sphere.Radius * std::max({ Scale.X, Scale.Y, Scale.Z });

			if (bDrawBodyWireframe)
			{
				BuildPhysicsSphereLines(CachedPhysicsAssetLines, Center, Radius);
			}
			if (bDrawBodySolid)
			{
				BuildPhysicsSphereSolid(CachedPhysicsAssetSolidVertices, CachedPhysicsAssetSolidIndices,
					Center, Sphere.Radius * Scale.X, Sphere.Radius * Scale.Y, Sphere.Radius * Scale.Z,
					AxisX, AxisY, AxisZ, BodySolidColor);
			}
		}

		for (const FKBoxElem& Box : AggGeom.BoxElems)
		{
			const FMatrix ShapeWorldMatrix = Box.Transform.ToMatrix() * BoneWorldMatrix;
			FVector Center, AxisX, AxisY, AxisZ, Scale;
			ExtractTransformAxes(ShapeWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);
			const FVector Extent(Box.Extent.X * Scale.X, Box.Extent.Y * Scale.Y, Box.Extent.Z * Scale.Z);

			if (bDrawBodyWireframe)
			{
				BuildPhysicsBoxLines(CachedPhysicsAssetLines, Center, Extent, AxisX, AxisY, AxisZ);
			}
			if (bDrawBodySolid)
			{
				BuildPhysicsBoxSolid(CachedPhysicsAssetSolidVertices, CachedPhysicsAssetSolidIndices,
					Center, Extent, AxisX, AxisY, AxisZ, BodySolidColor);
			}
		}

		for (const FKSphylElem& Sphyl : AggGeom.SphylElems)
		{
			const FMatrix ShapeWorldMatrix = Sphyl.Transform.ToMatrix() * BoneWorldMatrix;
			FVector Center, AxisX, AxisY, AxisZ, Scale;
			ExtractTransformAxes(ShapeWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);
			const float Radius = Sphyl.Radius * std::max(Scale.X, Scale.Y);
			const float HalfHeight = Sphyl.Length * 0.5f * Scale.Z + Radius;

			if (bDrawBodyWireframe)
			{
				BuildPhysicsCapsuleLines(CachedPhysicsAssetLines, Center, Radius, HalfHeight, AxisX, AxisY, AxisZ);
			}
			if (bDrawBodySolid)
			{
				BuildPhysicsCapsuleSolid(CachedPhysicsAssetSolidVertices, CachedPhysicsAssetSolidIndices,
					Center, Sphyl.Radius * Scale.X, Sphyl.Radius * Scale.Y, Sphyl.Radius * Scale.Z,
					std::max(0.0f, Sphyl.Length * 0.5f * Scale.Z), AxisX, AxisY, AxisZ, BodySolidColor);
			}
		}
	}

	// Constraint None 모드에서는 joint limit geometry를 만들지 않습니다.
	if (!bDrawConstraintSolid)
	{
		return;
	}

	const FVector4 SwingConeColor(1.0f, 0.08f, 0.05f, 0.26f);
	const FVector4 SwingArcColor(1.0f, 0.08f, 0.05f, 0.58f);
	const FVector4 TwistSectorColor(0.05f, 0.9f, 0.18f, 0.50f);

	// Constraint limit은 parent/child frame을 모두 반영한 표시 좌표계에 생성합니다.
	for (const FConstraintSetup& Constraint : PhysicsAsset->ConstraintSetups)
	{
		const int32 ParentBoneIndex = SMC->FindBoneIndex(Constraint.ParentBoneName.ToString());
		const int32 ChildBoneIndex = SMC->FindBoneIndex(Constraint.ChildBoneName.ToString());
		if (ParentBoneIndex < 0 || ChildBoneIndex < 0) continue;

		FTransform ParentBoneWorldTransform;
		FTransform ChildBoneWorldTransform;
		if (!SMC->GetBoneWorldTransformByIndex(ParentBoneIndex, ParentBoneWorldTransform)
			|| !SMC->GetBoneWorldTransformByIndex(ChildBoneIndex, ChildBoneWorldTransform))
		{
			continue;
		}

		const FMatrix ConstraintWorldMatrix = BuildConstraintDisplayWorldMatrix(Constraint, ParentBoneWorldTransform, ChildBoneWorldTransform);
		FVector Center, AxisX, AxisY, AxisZ, Scale;
		ExtractTransformAxes(ConstraintWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);

		const float BoneDistance = FVector::Distance(ParentBoneWorldTransform.Location, ChildBoneWorldTransform.Location);
		const float AutoRadius = FMath::Clamp(BoneDistance * 0.35f, 0.025f, 0.35f);
		const float Radius = AutoRadius * 0.3f;

		const float Swing1Degrees = GetConstraintVisualLimitDegrees(
			Constraint.Option.Swing1Motion, Constraint.Option.Swing1LimitDegrees, 89.0f);
		const float Swing2Degrees = GetConstraintVisualLimitDegrees(
			Constraint.Option.Swing2Motion, Constraint.Option.Swing2LimitDegrees, 89.0f);
		const float TwistDegrees = GetConstraintVisualLimitDegrees(
			Constraint.Option.TwistMotion, Constraint.Option.TwistLimitDegrees, 180.0f);

		if (Swing1Degrees > 0.0f || Swing2Degrees > 0.0f)
		{
			BuildConstraintSwingCone(CachedPhysicsConstraintSolidVertices, CachedPhysicsConstraintSolidIndices,
				Center, AxisX, AxisY, AxisZ, Radius, Swing1Degrees, Swing2Degrees, SwingConeColor, SwingArcColor);
		}
		if (TwistDegrees > 0.0f)
		{
			BuildConstraintSolidSector(CachedPhysicsConstraintSolidVertices, CachedPhysicsConstraintSolidIndices,
				Center, AxisY, AxisZ, Radius * 0.86f, TwistDegrees, TwistSectorColor);
		}

		AddPhysicsDebugLine(CachedPhysicsAssetLines, Center, ChildBoneWorldTransform.Location);
	}
}

bool FSkeletalMeshSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	if (!SMC) return false;

	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	const TArray<FVertexPNCTT>& SkinnedVertices = SMC->GetSkinnedVertices();
	const uint32 VertexCount = static_cast<uint32>(SkinnedVertices.size());
	if (VertexCount == 0) return false;

	if (bDynamicBufferNeedsCreate || !DynamicVertexBuffer.GetBuffer())
	{
		if (!DynamicVertexBuffer.Create(Device, CachedDynamicVertexCount ? CachedDynamicVertexCount : VertexCount, sizeof(FVertexPNCTT)))
		{
			return false;
		}
		bDynamicBufferNeedsCreate = false;
	}

	if (!DynamicVertexBuffer.EnsureCapacity(Device, VertexCount))
	{
		return false;
	}

	const uint64 CurrentRevision = SMC->GetSkinnedRevision();
	if (UploadedSkinnedRevision != CurrentRevision)
	{
		if (!DynamicVertexBuffer.Update(Context, SkinnedVertices.data(), VertexCount))
		{
			return false;
		}
		UploadedSkinnedRevision = CurrentRevision;
	}

	OutBuffer = {};
	OutBuffer.VB = DynamicVertexBuffer.GetBuffer();
	OutBuffer.VBStride = DynamicVertexBuffer.GetStride();
	OutBuffer.IB = Asset->RenderBuffer->GetIndexBuffer().GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

bool FSkeletalMeshSceneProxy::PrepareGpuSkinningDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset || !Asset->RenderBuffer || !Asset->RenderBuffer->IsValid()) return false;

	if (!UpdateSkinMatrixBuffer(Device, Context)) return false;

	OutBuffer = {};
	OutBuffer.VB = Asset->RenderBuffer->GetVertexBuffer().GetBuffer();
	OutBuffer.VBStride = Asset->RenderBuffer->GetVertexBuffer().GetStride();
	OutBuffer.IB = Asset->RenderBuffer->GetIndexBuffer().GetBuffer();
	return OutBuffer.VB != nullptr && OutBuffer.IB != nullptr;
}

ID3D11ShaderResourceView* FSkeletalMeshSceneProxy::GetSkinMatrixSRV(ID3D11Device* Device, ID3D11DeviceContext* Context) const
{
	UpdateSkinMatrixBuffer(Device, Context);
	return SkinMatrixSRV;
}

void FSkeletalMeshSceneProxy::ReleaseSkinMatrixBuffer() const
{
	if (SkinMatrixSRV)
	{
		SkinMatrixSRV->Release();
		SkinMatrixSRV = nullptr;
	}

	if (SkinMatrixBuffer)
	{
		SkinMatrixBuffer->Release();
		SkinMatrixBuffer = nullptr;
	}

	SkinMatrixCapacity = 0;
}

bool FSkeletalMeshSceneProxy::UpdateSkinMatrixBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context) const
{
	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC ? SMC->GetSkeletalMesh() : nullptr;
	FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Device || !Context || !SMC || !Asset || Asset->Bones.empty()) return false;

	const uint32 MatrixCount = static_cast<uint32>(Asset->Bones.size());
	const uint64 CurrentRevision = SMC->GetSkinnedRevision();

	if (!SkinMatrixBuffer || !SkinMatrixSRV || SkinMatrixCapacity < MatrixCount)
	{
		ReleaseSkinMatrixBuffer();

		D3D11_BUFFER_DESC BufferDesc = {};
		BufferDesc.ByteWidth = sizeof(FMatrix) * MatrixCount;
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		BufferDesc.StructureByteStride = sizeof(FMatrix);

		if (FAILED(Device->CreateBuffer(&BufferDesc, nullptr, &SkinMatrixBuffer)))
		{
			ReleaseSkinMatrixBuffer();
			return false;
		}

		SkinMatrixBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen("SkinMatrixBuffer")), "SkinMatrixBuffer");

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = MatrixCount;

		if (FAILED(Device->CreateShaderResourceView(SkinMatrixBuffer, &SRVDesc, &SkinMatrixSRV)))
		{
			ReleaseSkinMatrixBuffer();
			return false;
		}

		SkinMatrixSRV->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen("SkinMatrixSRV")), "SkinMatrixSRV");
		SkinMatrixCapacity = MatrixCount;
		UploadedSkinMatrixRevision = 0;
	}

	if (UploadedSkinMatrixRevision == CurrentRevision)
	{
		return true;
	}

	TArray<FMatrix> SkinMatrices;
	SMC->BuildSkinMatrices(SkinMatrices);
	if (SkinMatrices.size() != MatrixCount) return false;

	{
		SCOPE_STAT_CAT("GPUSkinning_MatrixUpload", "Skinning");

		D3D11_MAPPED_SUBRESOURCE Mapped = {};
		if (FAILED(Context->Map(SkinMatrixBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			return false;
		}

		std::memcpy(Mapped.pData, SkinMatrices.data(), sizeof(FMatrix) * MatrixCount);
		Context->Unmap(SkinMatrixBuffer, 0);
	}

	UploadedSkinMatrixRevision = CurrentRevision;
	return true;
}

void FSkeletalMeshSceneProxy::RebuildSectionDraws()
{
	SectionDraws.clear();

	USkeletalMeshComponent* SMC = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = SMC->GetSkeletalMesh();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		MeshBuffer = nullptr;
		SectionDraws.clear();

		return;
	}

	SectionDraws.clear();

	const auto& Slots = Mesh->GetSkeletalMaterials();
	const auto& Overrides = SMC->GetOverrideMaterials();

	for (const FSkeletalMeshSection& Section : Mesh->GetSkeletalMeshAsset()->Sections)
	{
		FMeshSectionDraw Draw;
		Draw.Material = nullptr;
		Draw.FirstIndex = Section.FirstIndex;
		Draw.IndexCount = Section.IndexCount;


		int32 i = Section.MaterialIndex;
		if (i >= 0 && i < static_cast<int32>(Slots.size()))
		{
			if (i < static_cast<int32>(Overrides.size()) && Overrides[i])
				Draw.Material = Overrides[i];
			else if (Slots[i].MaterialInterface)
				Draw.Material = Slots[i].MaterialInterface;
		}

		if (!Draw.Material)
		{
			Draw.Material = FMaterialManager::Get().GetOrCreateMaterial("None");
		}

		SectionDraws.push_back(Draw);
	}
}
