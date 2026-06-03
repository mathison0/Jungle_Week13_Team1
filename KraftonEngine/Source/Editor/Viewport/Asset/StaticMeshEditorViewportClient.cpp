#include "StaticMeshEditorViewportClient.h"

#include "Component/Primitive/StaticMeshComponent.h"
#include "Input/InputSystem.h"
#include "Math/MathUtils.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Render/Scene/FScene.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"
#include "Viewport/Viewport.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <unordered_set>

void FStaticMeshEditorViewportClient::Initialize(ID3D11Device* Device, uint32 Width, uint32 Height)
{
	Viewport = new FViewport();
	Viewport->Initialize(Device, Width, Height);
	Viewport->SetClient(this);

	bIsRenderable = true;
}

void FStaticMeshEditorViewportClient::Release()
{
	if (Viewport)
	{
		Viewport->Release();
		delete Viewport;
		Viewport = nullptr;
	}

	PreviewWorld = nullptr;
	PreviewActor = nullptr;
	PreviewMeshComponent = nullptr;
	CachedCollisionPreviewAsset = nullptr;
	TriangleCollisionPreviewLines.clear();
	bShowTriangleCollision = false;
	bTriangleCollisionOnly = false;
	bIsRenderable = false;
}

void FStaticMeshEditorViewportClient::ResetCameraToPreviewBounds()
{
	FBoundingBox Bounds = PreviewMeshComponent
		? PreviewMeshComponent->GetWorldBoundingBox()
		: FBoundingBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f));

	FVector Center = Bounds.GetCenter();
	float Radius = Bounds.GetExtent().Length();
	if (Radius < 0.1f)
	{
		Radius = 1.0f;
	}

	const float FovRadians = ViewTransform.FOV;
	const float Distance = Radius / std::tan(FovRadians * 0.5f) * 1.25f;
	const FVector ViewDir = FVector(-1.0f, -1.0f, -0.6f).Normalized();

	ViewTransform.ViewLocation = Center - ViewDir * Distance;
	ViewTransform.LookAt(Center);

	TargetLocation = ViewTransform.ViewLocation;
	LastAppliedCameraLocation = ViewTransform.ViewLocation;
	bTargetLocationInitialized = true;
	bLastAppliedCameraLocationInitialized = true;
}

bool FStaticMeshEditorViewportClient::IsMouseOverViewport() const
{
	if (!bIsRenderable || ViewportScreenRect.Width <= 0.0f || ViewportScreenRect.Height <= 0.0f) return false;

	ImVec2 MousePos = ImGui::GetMousePos();
	return MousePos.x >= ViewportScreenRect.X && MousePos.x <= (ViewportScreenRect.X + ViewportScreenRect.Width) &&
		MousePos.y >= ViewportScreenRect.Y && MousePos.y <= (ViewportScreenRect.Y + ViewportScreenRect.Height);
}

void FStaticMeshEditorViewportClient::NotifyViewportResized(int32 NewWidth, int32 NewHeight)
{
	if (Viewport && NewHeight > 0)
	{
		ViewTransform.AspectRatio = static_cast<float>(NewWidth) / static_cast<float>(NewHeight);
	}
}

bool FStaticMeshEditorViewportClient::GetCameraView(FMinimalViewInfo& OutPOV) const
{
	OutPOV.Location = ViewTransform.ViewLocation;
	OutPOV.Rotation = ViewTransform.ViewRotation;
	OutPOV.FOV = ViewTransform.FOV;
	OutPOV.AspectRatio = ViewTransform.AspectRatio;
	return true;
}

void FStaticMeshEditorViewportClient::SetShowTriangleCollision(bool bEnabled)
{
	bShowTriangleCollision = bEnabled;
	RenderOptions.ShowFlags.bStaticMeshTriangleCollision = bEnabled;
	if (!bShowTriangleCollision)
	{
		// overlay를 끄면서 렌더 메시가 계속 숨겨져 있으면 빈 preview처럼 보인다.
		// Collision Only 상태도 함께 해제하여 원래 렌더 메시를 복구한다.
		SetTriangleCollisionOnly(false);
	}
	else if (PreviewMeshComponent)
	{
		PreviewMeshComponent->SetVisibility(!bTriangleCollisionOnly);
		PreviewMeshComponent->MarkProxyDirty(EDirtyFlag::Mesh);
	}
}

void FStaticMeshEditorViewportClient::SetTriangleCollisionOnly(bool bEnabled)
{
	bTriangleCollisionOnly = bShowTriangleCollision && bEnabled;
	if (PreviewMeshComponent)
	{
		// Collision Only는 preview 대상 렌더 메시만 숨긴다.
		// preview world의 바닥 메시와 collision overlay line은 그대로 남겨 형태와 공간 기준을 확인할 수 있다.
		// SetVisibility()를 사용하므로 scene proxy visibility 갱신도 기존 component 흐름을 그대로 따른다.
		PreviewMeshComponent->SetVisibility(!bTriangleCollisionOnly);
	}
}

void FStaticMeshEditorViewportClient::SubmitPreviewDebugDraw(FScene& Scene)
{
	// overlay가 꺼져 있으면 edge cache를 만들지도 않고 frame line도 제출하지 않는다.
	// triangle 수가 많은 맵을 열어도 사용자가 시각화를 요청하기 전에는 추가 비용이 없다.
	if (!bShowTriangleCollision || !RenderOptions.ShowFlags.bStaticMeshTriangleCollision || !PreviewMeshComponent)
	{
		return;
	}

	UStaticMesh* StaticMesh = PreviewMeshComponent->GetStaticMesh();
	// UI에서는 collision이 활성화된 asset에서만 토글을 노출하지만, 렌더 직전에도 다시 확인한다.
	// Remove Triangle Collision 직후나 asset 교체 frame에서 존재하지 않는 collision을 그리지 않기 위해서다.
	if (!StaticMesh || !StaticMesh->IsTriangleMeshCollisionEnabled())
	{
		return;
	}

	const FStaticMesh* MeshAsset = StaticMesh->GetStaticMeshAsset();
	if (!MeshAsset)
	{
		return;
	}

	if (CachedCollisionPreviewAsset != MeshAsset)
	{
		// asset이 교체되었거나 아직 cache를 만들지 않은 경우에만 index 배열을 순회한다.
		// 일반 preview frame에서는 아래 world transform과 line 제출만 수행한다.
		RebuildTriangleCollisionPreviewLines(MeshAsset);
	}

	// cache는 asset local space에 저장한다. preview component의 transform을 매 frame 적용하면
	// component scale이나 transform이 달라져도 cook 입력을 복제하지 않고 올바른 위치에 그릴 수 있다.
	const FMatrix& WorldMatrix = PreviewMeshComponent->GetWorldMatrix();
	const FColor CollisionColor(60, 220, 255, 220);
	for (const FTriangleCollisionPreviewLine& Line : TriangleCollisionPreviewLines)
	{
		Scene.AddDebugLine(
			WorldMatrix.TransformPositionWithW(Line.Start),
			WorldMatrix.TransformPositionWithW(Line.End),
			CollisionColor);
	}
}

void FStaticMeshEditorViewportClient::RebuildTriangleCollisionPreviewLines(const FStaticMesh* MeshAsset)
{
	CachedCollisionPreviewAsset = MeshAsset;
	TriangleCollisionPreviewLines.clear();
	if (!MeshAsset || MeshAsset->Indices.size() < 3)
	{
		return;
	}

	// PhysX cooking 입력과 동일한 원본 StaticMesh triangle을 wire edge로 캐시한다.
	// cooked binary는 PhysX 내부 가속 구조가 포함된 전용 포맷이므로 시각화를 위해 역직렬화할 필요가 없다.
	// 현재 triangle collision은 렌더 메시 vertex/index를 그대로 cook하므로 원본 edge가 실제 collision 표면과 일치한다.
	//
	// 인접 triangle이 공유하는 index edge는 한 번만 저장한다. 삼각형마다 3개 선을 무조건 넣는 것보다
	// 큰 맵에서 cache 크기와 매 frame line 제출량을 줄일 수 있다.
	TriangleCollisionPreviewLines.reserve(MeshAsset->Indices.size());
	std::unordered_set<uint64> UniqueEdges;
	UniqueEdges.reserve(MeshAsset->Indices.size());
	auto AddUniqueEdge = [&](uint32 IndexA, uint32 IndexB)
	{
		// edge 방향은 wireframe 표시 결과에 영향을 주지 않는다.
		// 두 index를 정렬한 64-bit key를 사용해 A->B와 B->A를 같은 edge로 취급한다.
		const uint32 MinIndex = IndexA < IndexB ? IndexA : IndexB;
		const uint32 MaxIndex = IndexA < IndexB ? IndexB : IndexA;
		const uint64 EdgeKey = (static_cast<uint64>(MinIndex) << 32) | static_cast<uint64>(MaxIndex);
		if (!UniqueEdges.insert(EdgeKey).second)
		{
			return;
		}

		TriangleCollisionPreviewLines.push_back({
			MeshAsset->Vertices[IndexA].pos,
			MeshAsset->Vertices[IndexB].pos
		});
	};

	for (size_t IndexOffset = 0; IndexOffset + 2 < MeshAsset->Indices.size(); IndexOffset += 3)
	{
		const uint32 I0 = MeshAsset->Indices[IndexOffset];
		const uint32 I1 = MeshAsset->Indices[IndexOffset + 1];
		const uint32 I2 = MeshAsset->Indices[IndexOffset + 2];
		if (I0 >= MeshAsset->Vertices.size() || I1 >= MeshAsset->Vertices.size() || I2 >= MeshAsset->Vertices.size())
		{
			continue;
		}

		AddUniqueEdge(I0, I1);
		AddUniqueEdge(I1, I2);
		AddUniqueEdge(I2, I0);
	}
}

void FStaticMeshEditorViewportClient::Tick(float DeltaTime)
{
	SyncCameraSmoothingTarget();
	ApplySmoothedCameraLocation(DeltaTime);
	TickShortcuts();
	TickInput(DeltaTime);
}

void FStaticMeshEditorViewportClient::TickShortcuts()
{
	if (!FSlateApplication::Get().DoesClientOwnKeyboardInput(this)) return;

	if (InputSystem::Get().GetKeyDown('F'))
	{
		ResetCameraToPreviewBounds();
	}
}

void FStaticMeshEditorViewportClient::TickInput(float DeltaTime)
{
	if (!FSlateApplication::Get().DoesClientOwnMouseInput(this)) return;
	// 텍스트 입력 중에는 카메라 키/마우스 조작을 가로채지 않는다.
	if (ImGui::GetIO().WantTextInput) return;

	FViewportCameraControlSettings& ControlSettings = FEditorSettings::Get().MeshEditorViewportSettings.CameraControls;
	InputSystem& Input = InputSystem::Get();

	FVector LocalMove = FVector::ZeroVector;
	float WorldVerticalMove = 0.0f;
	const float CameraSpeed = ControlSettings.MoveSpeed;

	if (Input.GetKey('W')) LocalMove.X += CameraSpeed;
	if (Input.GetKey('S')) LocalMove.X -= CameraSpeed;
	if (Input.GetKey('D')) LocalMove.Y += CameraSpeed;
	if (Input.GetKey('A')) LocalMove.Y -= CameraSpeed;
	if (Input.GetKey('Q')) WorldVerticalMove -= CameraSpeed;
	if (Input.GetKey('E')) WorldVerticalMove += CameraSpeed;

	const FVector Forward = ViewTransform.ViewRotation.GetForwardVector();
	const FVector Right = ViewTransform.ViewRotation.GetRightVector();

	FVector DeltaMove = (Forward * LocalMove.X + Right * LocalMove.Y) * DeltaTime;
	DeltaMove.Z += WorldVerticalMove * DeltaTime;
	TargetLocation += DeltaMove;

	if (Input.GetKey(VK_RBUTTON))
	{
		const float MouseRotationSpeed = 0.15f * ControlSettings.RotationSpeed;
		const float DeltaYaw = static_cast<float>(Input.MouseDeltaX()) * MouseRotationSpeed;
		const float DeltaPitch = static_cast<float>(Input.MouseDeltaY()) * MouseRotationSpeed;
		ViewTransform.Rotate(DeltaYaw, DeltaPitch);
	}

	const float ScrollNotches = InputSystem::Get().GetScrollNotches();
	if (ScrollNotches != 0.0f)
	{
		if (InputSystem::Get().GetKey(VK_RBUTTON))
		{
			float& MoveSpeed = FEditorSettings::Get().MeshEditorViewportSettings.CameraControls.MoveSpeed;
			MoveSpeed = ScrollNotches < 0.0f ? MoveSpeed * 0.9f : MoveSpeed * 1.1f;
			MoveSpeed = Clamp(MoveSpeed, 0.001f, 1000.0f);
		}
		else if (ViewTransform.bIsOrtho)
		{
			const float NewWidth = ViewTransform.OrthoZoom - ScrollNotches * ControlSettings.ZoomSpeed * DeltaTime;
			ViewTransform.OrthoZoom = Clamp(NewWidth, 0.1f, 1000.0f);
		}
		else
		{
			TargetLocation += ViewTransform.ViewRotation.GetForwardVector() * (ScrollNotches * ControlSettings.ZoomSpeed * 0.015f);
		}
	}
}

void FStaticMeshEditorViewportClient::SyncCameraSmoothingTarget()
{
	const FVector CurrentLocation = ViewTransform.ViewLocation;
	const bool bCameraMovedExternally = bLastAppliedCameraLocationInitialized
		&& FVector::DistSquared(CurrentLocation, LastAppliedCameraLocation) > 0.0001f;

	if (!bTargetLocationInitialized || bCameraMovedExternally)
	{
		TargetLocation = CurrentLocation;
		bTargetLocationInitialized = true;
	}

	LastAppliedCameraLocation = CurrentLocation;
	bLastAppliedCameraLocationInitialized = true;
}

void FStaticMeshEditorViewportClient::ApplySmoothedCameraLocation(float DeltaTime)
{
	const FVector CurrentLocation = ViewTransform.ViewLocation;
	const float LerpAlpha = Clamp(DeltaTime * SmoothLocationSpeed, 0.0f, 1.0f);
	const FVector NewLocation = CurrentLocation + (TargetLocation - CurrentLocation) * LerpAlpha;
	ViewTransform.ViewLocation = NewLocation;

	LastAppliedCameraLocation = NewLocation;
	bLastAppliedCameraLocationInitialized = true;
}
