
#include "BoneDebugSceneProxy.h"

#include "Component/Debug/BoneDebugComponent.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Render/Types/FrameContext.h"

#include <algorithm>
#include <cmath>

#pragma region Line Draw
/* ============================================================================
 * [Wireframe & Solid 생성 유틸리티]
 * 디버그 렌더링을 위한 기하학적 형태(선, 원, 박스, 캡슐, 구 등)의
 * 정점(Vertex)과 인덱스(Index) 데이터를 구축하는 함수들의 모음입니다.
 * ============================================================================ */

 // 두 점을 연결하는 단일 선분을 추가합니다.
	static void AddLine(TArray<FWireLine>& Lines, const FVector& A, const FVector& B)
{
	Lines.push_back({ A, B });
}

// 지정된 중심(Center)과 두 축(AxisA, AxisB)을 평면으로 하는 360도 와이어프레임 원을 그립니다.
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

// 특정 각도(StartAngle)부터 시작하여 180도(Half-Circle) 와이어프레임을 그립니다. 주로 캡슐의 둥근 끝부분에 사용됩니다.
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

// 본(Bone)의 관절(Joint) 위치를 표시하기 위한 저폴리곤 와이어프레임 구(직교하는 3개의 원)를 생성합니다.
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

// 피직스 바디(Sphere)를 표현하기 위한 고해상도 와이어프레임 구를 생성합니다.
static void BuildPhysicsSphereLines(TArray<FWireLine>& Lines, const FVector& Center, float Radius)
{
	constexpr int32 Segments = 24;
	AddWireCircle(Lines, Center, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), Radius, Segments);
	AddWireCircle(Lines, Center, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), Radius, Segments);
	AddWireCircle(Lines, Center, FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), Radius, Segments);
}

// 피직스 바디(Box)를 표현하기 위한 와이어프레임 박스를 8개의 코너를 계산하여 생성합니다.
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

	// 상단 및 하단 사각형 렌더링
	AddLine(Lines, Corners[0], Corners[1]); AddLine(Lines, Corners[1], Corners[3]);
	AddLine(Lines, Corners[3], Corners[2]); AddLine(Lines, Corners[2], Corners[0]);
	AddLine(Lines, Corners[4], Corners[5]); AddLine(Lines, Corners[5], Corners[7]);
	AddLine(Lines, Corners[7], Corners[6]); AddLine(Lines, Corners[6], Corners[4]);

	// 기둥 부분 렌더링
	AddLine(Lines, Corners[0], Corners[4]); AddLine(Lines, Corners[1], Corners[5]);
	AddLine(Lines, Corners[2], Corners[6]); AddLine(Lines, Corners[3], Corners[7]);
}

// 피직스 바디(Capsule)를 표현하기 위한 와이어프레임 캡슐을 생성합니다. (원통부 + 상/하단 반구)
static void BuildPhysicsCapsuleLines(TArray<FWireLine>& Lines, const FVector& Center, float Radius, float HalfHeight,
	const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ)
{
	if (Radius <= 0.0f || HalfHeight <= 0.0f) return;

	const float CylinderHalf = std::max(0.0f, HalfHeight - Radius);
	constexpr int32 Segments = 24;
	constexpr int32 HalfSegments = 12;

	const FVector TopCenter = Center + AxisZ * CylinderHalf;
	const FVector BotCenter = Center - AxisZ * CylinderHalf;

	// 상/하단 캡슐 경계 원
	AddWireCircle(Lines, TopCenter, AxisX, AxisY, Radius, Segments);
	AddWireCircle(Lines, BotCenter, AxisX, AxisY, Radius, Segments);

	// 원통 기둥을 구성하는 4개의 세로선
	AddLine(Lines, TopCenter + AxisX * Radius, BotCenter + AxisX * Radius);
	AddLine(Lines, TopCenter - AxisX * Radius, BotCenter - AxisX * Radius);
	AddLine(Lines, TopCenter + AxisY * Radius, BotCenter + AxisY * Radius);
	AddLine(Lines, TopCenter - AxisY * Radius, BotCenter - AxisY * Radius);

	// 상단 돔(Dome) 
	AddWireHalfCircle(Lines, TopCenter, AxisX, AxisZ, Radius, HalfSegments, 0.0f);
	AddWireHalfCircle(Lines, TopCenter, AxisY, AxisZ, Radius, HalfSegments, 0.0f);

	// 하단 돔(Dome)
	AddWireHalfCircle(Lines, BotCenter, AxisX, AxisZ, Radius, HalfSegments, FMath::Pi);
	AddWireHalfCircle(Lines, BotCenter, AxisY, AxisZ, Radius, HalfSegments, FMath::Pi);
}

// Solid 렌더링을 위한 단일 정점(Vertex)을 버퍼에 추가합니다.
static void AddSolidVertex(TArray<FVertex>& Vertices, const FVector& Position, const FVector4& Color)
{
	Vertices.push_back({ Position, Color, 0 });
}

// Solid 렌더링을 위한 삼각형 폴리곤을 구성하고 인덱스를 추가합니다.
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

// Swing1(Z) / Swing2(Y)를 함께 반영하는 타원 cone 형태의 가동 범위를 시각화합니다.
static void BuildConstraintSwingCone(TArray<FVertex>& Vertices, TArray<uint32>& Indices,
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

// 물리 제약 조건 중 Swing2(평면상의 부채꼴 가동 범위)를 시각화합니다.
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

// 제약 조건의 모션 타입(Free, Limited, Locked)에 따라 렌더링할 실제 각도를 반환합니다.
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

// Solid 렌더링용 피직스 박스 메쉬 데이터를 구축합니다.
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

	// 6개의 면을 구성하는 12개의 삼각형 인덱스
	static constexpr int32 FaceTris[][3] =
	{
		{0, 1, 3}, {0, 3, 2}, // Front
		{4, 7, 5}, {4, 6, 7}, // Back
		{0, 4, 1}, {1, 4, 5}, // Bottom
		{2, 3, 6}, {3, 7, 6}, // Top
		{0, 2, 4}, {2, 6, 4}, // Left
		{1, 5, 3}, {3, 5, 7}  // Right
	};

	for (const auto& Tri : FaceTris)
	{
		AddSolidTriangle(Vertices, Indices, Corners[Tri[0]], Corners[Tri[1]], Corners[Tri[2]], Color);
	}
}

// 구면 좌표계를 이용해 타원체(Ellipsoid) 표면의 한 점을 계산합니다. 비균등 스케일링된 구 렌더링 시 사용됩니다.
static FVector MakeEllipsoidPoint(const FVector& Center, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ,
	float RadiusX, float RadiusY, float RadiusZ, float Theta, float Phi)
{
	const float SinPhi = sinf(Phi);
	return Center
		+ AxisX * (RadiusX * SinPhi * cosf(Theta))
		+ AxisY * (RadiusY * SinPhi * sinf(Theta))
		+ AxisZ * (RadiusZ * cosf(Phi));
}

// Solid 렌더링용 피직스 구(Sphere) 메쉬 데이터를 구축합니다.
static void BuildPhysicsSphereSolid(TArray<FVertex>& Vertices, TArray<uint32>& Indices, const FVector& Center,
	float RadiusX, float RadiusY, float RadiusZ, const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ, const FVector4& Color)
{
	constexpr int32 Slices = 16; // 경도 분할 수
	constexpr int32 Stacks = 8;  // 위도 분할 수

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

			// 극점(Pole)에서는 삼각형 하나, 중간에서는 사각형(삼각형 2개) 렌더링
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

// 캡슐의 돔(반구) 표면 상의 점을 계산합니다.
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

// Solid 렌더링용 피직스 캡슐(Capsule) 메쉬 데이터를 구축합니다.
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

		// 중앙 원통 부분 렌더링
		AddSolidTriangle(Vertices, Indices, Top0, Bot0, Top1, Color);
		AddSolidTriangle(Vertices, Indices, Top1, Bot0, Bot1, Color);

		// 상/하단 돔 렌더링
		for (int32 Stack = 0; Stack < CapStacks; ++Stack)
		{
			const float Phi0 = (FMath::Pi * 0.5f) * static_cast<float>(Stack) / static_cast<float>(CapStacks);
			const float Phi1 = (FMath::Pi * 0.5f) * static_cast<float>(Stack + 1) / static_cast<float>(CapStacks);

			// 상단 반구
			const FVector T00 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, true, Theta0, Phi0);
			const FVector T01 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, true, Theta1, Phi0);
			const FVector T10 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, true, Theta0, Phi1);
			const FVector T11 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, true, Theta1, Phi1);
			AddSolidTriangle(Vertices, Indices, T00, T10, T01, Color);
			AddSolidTriangle(Vertices, Indices, T01, T10, T11, Color);

			// 하단 반구
			const FVector B00 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, false, Theta0, Phi0);
			const FVector B01 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, false, Theta1, Phi0);
			const FVector B10 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, false, Theta0, Phi1);
			const FVector B11 = MakeCapsuleRingPoint(Center, AxisX, AxisY, AxisZ, RadiusX, RadiusY, RadiusZ, CylinderHalf, false, Theta1, Phi1);
			AddSolidTriangle(Vertices, Indices, B00, B01, B10, Color);
			AddSolidTriangle(Vertices, Indices, B01, B11, B10, Color);
		}
	}
}

// 부모 본(Start)에서 자식 본(End)을 향하는 피라미드 형태의 와이어프레임을 그립니다. 뼈대의 방향성을 시각화합니다.
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
	const FVector Center = End; // 피라미드의 밑면 중심 위치

	// 피라미드 밑면의 4개 꼭지점
	const FVector C0 = Center + Right * HalfWidth + Up * HalfWidth;
	const FVector C1 = Center - Right * HalfWidth + Up * HalfWidth;
	const FVector C2 = Center - Right * HalfWidth - Up * HalfWidth;
	const FVector C3 = Center + Right * HalfWidth - Up * HalfWidth;

	// 시작점에서 밑면의 각 꼭지점으로 선을 긋습니다 (피라미드 측면)
	AddLine(Lines, Start, C0);
	AddLine(Lines, Start, C1);
	AddLine(Lines, Start, C2);
	AddLine(Lines, Start, C3);

	// 밑면의 테두리 선을 긋습니다.
	AddLine(Lines, C0, C1);
	AddLine(Lines, C1, C2);
	AddLine(Lines, C2, C3);
	AddLine(Lines, C3, C0);
}

// 축(Axis) 벡터를 정규화하고 기존 길이를 반환합니다. 
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

// 트랜스폼 행렬(Matrix)로부터 위치(Center), 회전 축(AxisX, Y, Z) 그리고 스케일(Scale)을 분리 추출합니다.
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

static FMatrix BuildConstraintDisplayWorldMatrix(const FConstraintSetup& Constraint, const FTransform& ParentBoneWorldTransform, const FTransform& ChildBoneWorldTransform)
{
	const FTransform ParentFrameWorld(Constraint.ParentFrame.ToMatrix() * ParentBoneWorldTransform.ToMatrix());
	const FTransform ChildFrameWorld(Constraint.ChildFrame.ToMatrix() * ChildBoneWorldTransform.ToMatrix());

	const FTransform DisplayTransform(
		FVector::Lerp(ParentFrameWorld.Location, ChildFrameWorld.Location, 0.5f),
		FQuat::Slerp(ParentFrameWorld.Rotation.GetNormalized(), ChildFrameWorld.Rotation.GetNormalized(), 0.5f).GetNormalized(),
		FVector::Lerp(ParentFrameWorld.Scale, ChildFrameWorld.Scale, 0.5f));
	return DisplayTransform.ToMatrix();
}

#pragma endregion


/* ============================================================================
 * [FBoneDebugSceneProxy]
 * 렌더 스레드(Render Thread)에서 본 구조와 물리 데이터 시각화를 담당하는 프록시 클래스입니다.
 * ============================================================================ */

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

void FBoneDebugSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	ViewportPhysicsAssetBodyShowMode = Frame.RenderOptions.PhysicsAssetBodyShowMode;
	ViewportPhysicsAssetConstraintShowMode = Frame.RenderOptions.PhysicsAssetConstraintShowMode;
	RebuildLines(); // 매 뷰포트 업데이트마다 렌더 데이터를 갱신합니다.
}

// 스켈레탈 메시의 뼈대(Bone) 계층을 바탕으로 렌더링할 와이어프레임 선들을 다시 생성합니다.
void FBoneDebugSceneProxy::RebuildLines()
{
	// 기존 생성된 캐시 데이터를 모두 초기화합니다.
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

	// 피직스 에셋 시각화 데이터를 먼저 빌드합니다.
	RebuildPhysicsAssetLines(Comp, MeshComp, Asset);

	const int32 BoneCount = static_cast<int32>(Asset->Bones.size());
	if (BoneCount <= 0) return;

	// 모델 전체 크기에 비례하여 조인트(구)와 뼈대(피라미드)의 렌더링 크기를 조절합니다.
	const FBoundingBox Bounds = MeshComp->GetWorldBoundingBox();
	const FVector Extent = Bounds.GetExtent();
	const float ModelSize = Extent.Length();

	const float JointRadius = ModelSize * 0.01f;
	const float PyramidWidthScale = 0.03f;

	// [모드 1] 모든 뼈대를 렌더링
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

	// [모드 2] 선택된 특정 뼈대(Selected Bone)와 그 주변 계층(자식, 부모)만 렌더링
	const int32 BoneIndex = Comp->GetSelectedBoneIndex();
	if (BoneIndex < 0 || BoneIndex >= BoneCount) return;

	const FVector BonePos = MeshComp->GetBoneLocationByIndex(BoneIndex);
	BuildLowSphereLines(CachedLines, BonePos, JointRadius); // 현재 선택된 조인트 강조

	// 자식 본들을 찾아 피라미드로 연결
	for (int32 i = 0; i < BoneCount; ++i)
	{
		if (Asset->Bones[i].ParentIndex == BoneIndex)
		{
			const FVector ChildPos = MeshComp->GetBoneLocationByIndex(i);
			BuildBonePyramidLines(CachedLines, ChildPos, BonePos, PyramidWidthScale);
		}
	}

	// 부모 본을 찾아 피라미드로 연결 (별도의 색상을 위해 CachedParentBoneLines 배열 사용)
	if (Asset->Bones[BoneIndex].ParentIndex != -1)
	{
		const int32 ParentIndex = Asset->Bones[BoneIndex].ParentIndex;
		const FVector ParentPos = MeshComp->GetBoneLocationByIndex(ParentIndex);
		BuildBonePyramidLines(CachedParentBoneLines, BonePos, ParentPos, PyramidWidthScale);
	}
}

// 피직스 바디(충돌체) 및 제약 조건(Constraints)의 시각화 데이터를 재구축합니다.
void FBoneDebugSceneProxy::RebuildPhysicsAssetLines(UBoneDebugComponent* Comp, USkeletalMeshComponent* MeshComp, const FSkeletalMesh* Asset)
{
	if (!Comp || !Comp->ShouldDrawPhysicsAsset() || !MeshComp || !Asset) return;

	USkeletalMesh* Mesh = MeshComp->GetSkeletalMesh();
	UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!PhysicsAsset) return;

	EPhysicsAssetBodyShowMode BodyShowMode = Comp->GetPhysicsAssetBodyShowMode();
	if (BodyShowMode != EPhysicsAssetBodyShowMode::None)
	{
		if (ViewportPhysicsAssetBodyShowMode == EPhysicsAssetBodyShowMode::None)
		{
			BodyShowMode = EPhysicsAssetBodyShowMode::None;
		}
		else if (ViewportPhysicsAssetBodyShowMode == EPhysicsAssetBodyShowMode::Wireframe)
		{
			BodyShowMode = EPhysicsAssetBodyShowMode::Wireframe;
		}
	}

	EPhysicsAssetConstraintShowMode ConstraintShowMode = Comp->GetPhysicsAssetConstraintShowMode();
	if (ViewportPhysicsAssetConstraintShowMode == EPhysicsAssetConstraintShowMode::None)
	{
		ConstraintShowMode = EPhysicsAssetConstraintShowMode::None;
	}
	// Solid body 모드에서는 wire/debug line을 만들지 않습니다.
	const bool bDrawBodyWireframe = BodyShowMode == EPhysicsAssetBodyShowMode::Wireframe;
	const bool bDrawBodySolid = BodyShowMode == EPhysicsAssetBodyShowMode::Solid;
	const bool bDrawConstraintSolid = ConstraintShowMode == EPhysicsAssetConstraintShowMode::Solid;
	const FVector4 UnselectedSolidColor(0.56f, 0.58f, 0.60f, 0.30f);
	const FVector4 SelectedSolidColor(0.25f, 0.76f, 1.00f, 0.30f); // 에디터에서 클릭된 바디는 파란색으로 하이라이트

	UBodySetup* SelectedBodySetup = Comp->GetSelectedPhysicsBodySetup();
	const int32 SelectedConstraintIndex = Comp->GetSelectedPhysicsConstraintIndex();

	// 1. 피직스 바디(Sphere, Box, Capsule) 렌더링
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

		// 충돌체 형태별 월드 변환 행렬을 적용하여 시각화 데이터 생성
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
					AxisX, AxisY, AxisZ, SolidColor);
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

			if (bDrawBodyWireframe)
			{
				BuildPhysicsCapsuleLines(CachedPhysicsAssetLines, Center, Radius, HalfHeight, AxisX, AxisY, AxisZ);
			}
			if (bDrawBodySolid)
			{
				BuildPhysicsCapsuleSolid(CachedPhysicsAssetSolidVertices, CachedPhysicsAssetSolidIndices, Center,
					Sphyl.Radius * Scale.X, Sphyl.Radius * Scale.Y, Sphyl.Radius * Scale.Z,
					std::max(0.0f, Sphyl.Length * 0.5f * Scale.Z), AxisX, AxisY, AxisZ, SolidColor);
			}
		}
	}

	// 2. 제약 조건(Constraints - Joint Limits) 렌더링을 위한 색상 정의
	const FVector4 SwingConeColor(1.0f, 0.08f, 0.05f, 0.26f); // Swing1/2 Red Cone
	const FVector4 SwingArcColor(1.0f, 0.08f, 0.05f, 0.58f);
	const FVector4 TwistSectorColor(0.05f, 0.9f, 0.18f, 0.50f); // Twist Green Sector

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

		// Parent/Child constraint frame을 함께 반영해서 두 프레임 중 어느 쪽을 편집해도 시각화가 따라오도록 합니다.
		const FMatrix ConstraintWorldMatrix = BuildConstraintDisplayWorldMatrix(Constraint, ParentBoneWorldTransform, ChildBoneWorldTransform);
		FVector Center, AxisX, AxisY, AxisZ, Scale;
		ExtractTransformAxes(ConstraintWorldMatrix, Center, AxisX, AxisY, AxisZ, Scale);

		// 두 뼈대 사이의 거리에 비례하여 제약 조건 렌더링 크기를 자동 조절
		const float BoneDistance = FVector::Distance(ParentBoneWorldTransform.Location, ChildBoneWorldTransform.Location);
		const float AutoRadius = FMath::Clamp(BoneDistance * 0.35f, 0.025f, 0.35f);

		// 에디터에서 선택된 제약조건은 살짝 크게 렌더링하여 강조
		const float Radius = AutoRadius * 0.3f * (ConstraintIndex == SelectedConstraintIndex ? 1.15f : 1.0f);

		if (bDrawConstraintSolid)
		{
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
		}

		// 제약 조건 중심축과 자식 본의 위치를 연결하는 기준선
		if (bDrawConstraintSolid)
		{
			AddLine(CachedPhysicsAssetLines, Center, ChildBoneWorldTransform.Location);
		}
	}
}

