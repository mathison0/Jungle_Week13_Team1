#include "BoneDebugSceneProxy.h"

#include "Component/Debug/BoneDebugComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"

#include <algorithm>
#include <cmath>

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

static void AddSolidVertex(TArray<FVertex>& Vertices, const FVector& Position, const FVector4& Color)
{
	Vertices.push_back({ Position, Color, 0 });
}

static void AddSolidTriangle(TArray<FVertex>& Vertices, TArray<uint32>& Indices,
	const FVector& A, const FVector& B, const FVector& C, const FVector4& Color)
{
	const uint32 BaseIndex = static_cast<uint32>(Vertices.size());
	AddSolidVertex(Vertices, A, Color);
	AddSolidVertex(Vertices, B, Color);
	AddSolidVertex(Vertices, C, Color);
	Indices.push_back(BaseIndex);
	Indices.push_back(BaseIndex + 1);
	Indices.push_back(BaseIndex + 2);
}

static void MakeBasisFromAxis(const FVector& InAxis, FVector& OutAxis, FVector& OutTangentA, FVector& OutTangentB)
{
	OutAxis = InAxis;
	if (OutAxis.IsNearlyZero())
	{
		OutAxis = FVector::XAxisVector;
	}
	OutAxis.Normalize();

	const FVector UpHint = std::fabs(OutAxis.Z) > 0.9f ? FVector::YAxisVector : FVector::ZAxisVector;
	OutTangentA = UpHint.Cross(OutAxis);
	if (OutTangentA.IsNearlyZero())
	{
		OutTangentA = FVector::XAxisVector;
	}
	OutTangentA.Normalize();
	OutTangentB = OutAxis.Cross(OutTangentA);
	OutTangentB.Normalize();
}

static void BuildConstraintConeAndArc(TArray<FVertex>& Vertices, TArray<uint32>& Indices,
	const FVector& Center, const FVector& ConeAxis,
	float ConeLength, float LimitDegrees, const FVector4& ConeColor, const FVector4& ArcColor)
{
	if (ConeLength <= 0.0f || LimitDegrees <= 0.0f) return;

	const float LimitRadians = FMath::Clamp(LimitDegrees, 0.0f, 89.0f) * FMath::Pi / 180.0f;
	const float BaseRadius = std::max(0.002f, tanf(LimitRadians) * ConeLength);
	const float ArcWidth = std::max(0.004f, ConeLength * 0.035f);
	constexpr int32 Segments = 36;

	FVector Axis, TangentA, TangentB;
	MakeBasisFromAxis(ConeAxis, Axis, TangentA, TangentB);

	const FVector BaseCenter = Center + Axis * ConeLength;
	FVector PrevInner = BaseCenter + TangentA * BaseRadius;
	FVector PrevOuter = BaseCenter + TangentA * (BaseRadius + ArcWidth);

	for (int32 Segment = 1; Segment <= Segments; ++Segment)
	{
		const float Angle = 2.0f * FMath::Pi * static_cast<float>(Segment) / static_cast<float>(Segments);
		const FVector RingDirection = TangentA * cosf(Angle) + TangentB * sinf(Angle);
		const FVector NextInner = BaseCenter + RingDirection * BaseRadius;
		const FVector NextOuter = BaseCenter + RingDirection * (BaseRadius + ArcWidth);

		AddSolidTriangle(Vertices, Indices, Center, PrevInner, NextInner, ConeColor);
		AddSolidTriangle(Vertices, Indices, PrevInner, PrevOuter, NextInner, ArcColor);
		AddSolidTriangle(Vertices, Indices, NextInner, PrevOuter, NextOuter, ArcColor);
		PrevInner = NextInner;
		PrevOuter = NextOuter;
	}
}

static void BuildConstraintSolidSector(TArray<FVertex>& Vertices, TArray<uint32>& Indices,
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

static void BuildConstraintTwistBand(TArray<FVertex>& Vertices, TArray<uint32>& Indices,
	const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
	float Radius, float BandHalfWidth, float LimitDegrees, const FVector4& Color)
{
	if (Radius <= 0.0f || BandHalfWidth <= 0.0f || LimitDegrees <= 0.0f) return;

	const float LimitRadians = FMath::Clamp(LimitDegrees, 0.0f, 180.0f) * FMath::Pi / 180.0f;
	constexpr int32 Segments = 28;
	FVector PrevInner = Center - AxisX * BandHalfWidth + (AxisY * cosf(-LimitRadians) + AxisZ * sinf(-LimitRadians)) * Radius;
	FVector PrevOuter = Center + AxisX * BandHalfWidth + (AxisY * cosf(-LimitRadians) + AxisZ * sinf(-LimitRadians)) * Radius;

	for (int32 Segment = 1; Segment <= Segments; ++Segment)
	{
		const float Alpha = static_cast<float>(Segment) / static_cast<float>(Segments);
		const float Angle = -LimitRadians + LimitRadians * 2.0f * Alpha;
		const FVector Arc = (AxisY * cosf(Angle) + AxisZ * sinf(Angle)) * Radius;
		const FVector NextInner = Center - AxisX * BandHalfWidth + Arc;
		const FVector NextOuter = Center + AxisX * BandHalfWidth + Arc;

		AddSolidTriangle(Vertices, Indices, PrevInner, NextInner, PrevOuter, Color);
		AddSolidTriangle(Vertices, Indices, PrevOuter, NextInner, NextOuter, Color);
		PrevInner = NextInner;
		PrevOuter = NextOuter;
	}
}

static float GetConstraintVisualLimitDegrees(EAngularConstraintMotion Motion, float LimitDegrees, float FreeDegrees)
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

static void BuildPhysicsBoxSolid(TArray<FVertex>& Vertices, TArray<uint32>& Indices, const FVector& Center, const FVector& Extent,
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
		{0, 1, 3}, {0, 3, 2},
		{4, 7, 5}, {4, 6, 7},
		{0, 4, 1}, {1, 4, 5},
		{2, 3, 6}, {3, 7, 6},
		{0, 2, 4}, {2, 6, 4},
		{1, 5, 3}, {3, 5, 7}
	};

	for (const auto& Tri : FaceTris)
	{
		AddSolidTriangle(Vertices, Indices, Corners[Tri[0]], Corners[Tri[1]], Corners[Tri[2]], Color);
	}
}

static FVector MakeEllipsoidPoint(const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
	float RadiusX, float RadiusY, float RadiusZ, float Theta, float Phi)
{
	const float SinPhi = sinf(Phi);
	return Center
		+ AxisX * (RadiusX * SinPhi * cosf(Theta))
		+ AxisY * (RadiusY * SinPhi * sinf(Theta))
		+ AxisZ * (RadiusZ * cosf(Phi));
}

static void BuildPhysicsSphereSolid(TArray<FVertex>& Vertices, TArray<uint32>& Indices, const FVector& Center,
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

static FVector MakeCapsuleRingPoint(const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
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

static void BuildPhysicsCapsuleSolid(TArray<FVertex>& Vertices, TArray<uint32>& Indices, const FVector& Center,
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
	CachedPhysicsAssetSolidVertices.clear();
	CachedPhysicsAssetSolidIndices.clear();
	CachedPhysicsConstraintSolidVertices.clear();
	CachedPhysicsConstraintSolidIndices.clear();

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

	const bool bDrawSolid = Comp->ShouldDrawPhysicsAssetSolid();
	const FVector4 UnselectedSolidColor(0.56f, 0.58f, 0.60f, 0.30f);
	const FVector4 SelectedSolidColor(0.25f, 0.76f, 1.00f, 0.30f);
	UBodySetup* SelectedBodySetup = Comp->GetSelectedPhysicsBodySetup();
	const int32 SelectedConstraintIndex = Comp->GetSelectedPhysicsConstraintIndex();

	for (UBodySetup* BodySetup : PhysicsAsset->BodySetups)
	{
		if (!BodySetup || !BodySetup->HasGeometry()) continue;
		const FVector4 SolidColor = (BodySetup == SelectedBodySetup) ? SelectedSolidColor : UnselectedSolidColor;

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
			if (bDrawSolid)
			{
				BuildPhysicsSphereSolid(CachedPhysicsAssetSolidVertices, CachedPhysicsAssetSolidIndices,
					Center, Sphere.Radius * Scale.X, Sphere.Radius * Scale.Y, Sphere.Radius * Scale.Z,
					AxisX, AxisY, AxisZ, SolidColor);
			}
		}

		for (const FKBoxElem& Box : AggGeom.BoxElems)
		{
			const FMatrix ShapeWorldMatrix = Box.Transform.ToMatrix() * BoneWorldMatrix;
			FVector Center, AxisX, AxisY, AxisZ, Scale;
			ExtractTransformAxes(ShapeWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);
			const FVector Extent(Box.Extent.X * Scale.X, Box.Extent.Y * Scale.Y, Box.Extent.Z * Scale.Z);
			BuildPhysicsBoxLines(CachedPhysicsAssetLines, Center, Extent, AxisX, AxisY, AxisZ);
			if (bDrawSolid)
			{
				BuildPhysicsBoxSolid(CachedPhysicsAssetSolidVertices, CachedPhysicsAssetSolidIndices, Center, Extent, AxisX, AxisY, AxisZ, SolidColor);
			}
		}

		for (const FKSphylElem& Sphyl : AggGeom.SphylElems)
		{
			const FMatrix ShapeWorldMatrix = Sphyl.Transform.ToMatrix() * BoneWorldMatrix;
			FVector Center, AxisX, AxisY, AxisZ, Scale;
			ExtractTransformAxes(ShapeWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);
			const float Radius = Sphyl.Radius * std::max(Scale.X, Scale.Y);
			const float HalfHeight = Sphyl.Length * 0.5f * Scale.Z + Radius;
			BuildPhysicsCapsuleLines(CachedPhysicsAssetLines, Center, Radius, HalfHeight, AxisX, AxisY, AxisZ);
			if (bDrawSolid)
			{
				BuildPhysicsCapsuleSolid(CachedPhysicsAssetSolidVertices, CachedPhysicsAssetSolidIndices, Center,
					Sphyl.Radius * Scale.X, Sphyl.Radius * Scale.Y, Sphyl.Radius * Scale.Z,
					std::max(0.0f, Sphyl.Length * 0.5f * Scale.Z), AxisX, AxisY, AxisZ, SolidColor);
			}
		}
	}

	const FVector4 Swing1ConeColor(1.0f, 0.08f, 0.05f, 0.26f);
	const FVector4 Swing1ArcColor(1.0f, 0.08f, 0.05f, 0.58f);
	const FVector4 Swing2ArcColor(0.05f, 0.9f, 0.18f, 0.58f);
	const FVector4 TwistColor(0.10f, 0.34f, 1.0f, 0.62f);

	for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(PhysicsAsset->ConstraintSetups.size()); ++ConstraintIndex)
	{
		const FConstraintSetup& Constraint = PhysicsAsset->ConstraintSetups[ConstraintIndex];

		const int32 ParentBoneIndex = MeshComp->FindBoneIndex(Constraint.ParentBoneName.ToString());
		const int32 ChildBoneIndex = MeshComp->FindBoneIndex(Constraint.ChildBoneName.ToString());
		if (ParentBoneIndex < 0 || ChildBoneIndex < 0) continue;

		FTransform ParentBoneWorldTransform;
		FTransform ChildBoneWorldTransform;
		if (!MeshComp->GetBoneWorldTransformByIndex(ParentBoneIndex, ParentBoneWorldTransform)
			|| !MeshComp->GetBoneWorldTransformByIndex(ChildBoneIndex, ChildBoneWorldTransform))
		{
			continue;
		}

		const FMatrix ConstraintWorldMatrix = Constraint.ParentFrame.ToMatrix() * ParentBoneWorldTransform.ToMatrix();
		FVector Center, AxisX, AxisY, AxisZ, Scale;
		ExtractTransformAxes(ConstraintWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);

		const float BoneDistance = FVector::Distance(ParentBoneWorldTransform.Location, ChildBoneWorldTransform.Location);
		const float AutoRadius = FMath::Clamp(BoneDistance * 0.35f, 0.025f, 0.35f);
		const float Radius = AutoRadius * 0.3f * (ConstraintIndex == SelectedConstraintIndex ? 1.15f : 1.0f);
		const float BandHalfWidth = std::max(0.01f, Radius * 0.08f);

		if (bDrawSolid)
		{
			const float Swing1Degrees = GetConstraintVisualLimitDegrees(
				Constraint.Option.Swing1Motion, Constraint.Option.Swing1LimitDegrees, 89.0f);
			const float Swing2Degrees = GetConstraintVisualLimitDegrees(
				Constraint.Option.Swing2Motion, Constraint.Option.Swing2LimitDegrees, 180.0f);
			const float TwistDegrees = GetConstraintVisualLimitDegrees(
				Constraint.Option.TwistMotion, Constraint.Option.TwistLimitDegrees, 180.0f);

			if (Swing1Degrees > 0.0f)
			{
				BuildConstraintConeAndArc(CachedPhysicsConstraintSolidVertices, CachedPhysicsConstraintSolidIndices,
					Center, AxisZ, Radius, Swing1Degrees, Swing1ConeColor, Swing1ArcColor);
			}
			if (Swing2Degrees > 0.0f)
			{
				BuildConstraintSolidSector(CachedPhysicsConstraintSolidVertices, CachedPhysicsConstraintSolidIndices,
					Center, AxisX, AxisZ, Radius * 0.92f, Swing2Degrees, Swing2ArcColor);
			}
			if (TwistDegrees > 0.0f)
			{
				BuildConstraintTwistBand(CachedPhysicsConstraintSolidVertices, CachedPhysicsConstraintSolidIndices,
					Center, AxisX, AxisY, AxisZ, Radius * 0.72f, BandHalfWidth, TwistDegrees, TwistColor);
			}
		}

		AddLine(CachedPhysicsAssetLines, Center, ChildBoneWorldTransform.Location);
	}
}
