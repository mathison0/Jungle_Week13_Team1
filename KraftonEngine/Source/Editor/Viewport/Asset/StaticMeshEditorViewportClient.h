#pragma once

#include "Viewport/EditorPreviewViewportClient.h"
#include "Viewport/ViewportClient.h"
#include "Editor/Viewport/ViewportCameraTransform.h"
#include "Editor/Slate/SWindow.h"

#include <d3d11.h>

class FWindowsWindow;
class FScene;
struct FStaticMesh;
class UStaticMeshComponent;
class UWorld;
class AActor;

class FStaticMeshEditorViewportClient : public FViewportClient, public IEditorPreviewViewportClient
{
public:
	void Initialize(ID3D11Device* Device, uint32 Width, uint32 Height);
	void Release();

	void ResetCameraToPreviewBounds();

	void SetPreviewWorld(UWorld* InWorld) { PreviewWorld = InWorld; }
	void SetPreviewActor(AActor* InActor) { PreviewActor = InActor; }
	void SetPreviewMeshComponent(UStaticMeshComponent* InComp) { PreviewMeshComponent = InComp; }
	void SetViewportRect(float X, float Y, float Width, float Height) { ViewportScreenRect = { X, Y, Width, Height }; }

	bool IsRenderable() const override { return bIsRenderable; }
	bool IsMouseOverViewport() const override;

	FViewport* GetViewport() const override { return Viewport; }
	UWorld* GetPreviewWorld() const override { return PreviewWorld; }

	FViewportRenderOptions& GetRenderOptions() override { return RenderOptions; }
	const FViewportRenderOptions& GetRenderOptions() const override { return RenderOptions; }

	void NotifyViewportResized(int32 NewWidth, int32 NewHeight) override;
	bool GetCameraView(FMinimalViewInfo& OutPOV) const override;

	// StaticMesh Editor preview frame에 triangle collision wireframe line을 제출한다.
	// 원본 vertex/index에서 만든 local-space edge cache를 component world transform으로 변환해 그린다.
	void SubmitPreviewDebugDraw(FScene& Scene) override;

	// Show Collision 토글. 끄면 Collision Only도 함께 해제하여 preview mesh가 다시 보이게 한다.
	void SetShowTriangleCollision(bool bEnabled);
	bool IsShowingTriangleCollision() const { return RenderOptions.ShowFlags.bStaticMeshTriangleCollision; }

	// Collision Only 토글. overlay는 유지하고 preview 대상 렌더 메시만 숨긴다.
	void SetTriangleCollisionOnly(bool bEnabled);
	bool IsShowingTriangleCollisionOnly() const { return bTriangleCollisionOnly; }

	void Tick(float DeltaTime);

private:
	void TickShortcuts();
	void TickInput(float DeltaTime);
	void SyncCameraSmoothingTarget();
	void ApplySmoothedCameraLocation(float DeltaTime);

	// PhysX cooking 입력과 동일한 StaticMesh triangle index를 읽어 중복 없는 wire edge cache를 만든다.
	// cache는 overlay를 처음 표시할 때 한 번 만들고 이후 preview frame에서 재사용한다.
	void RebuildTriangleCollisionPreviewLines(const FStaticMesh* MeshAsset);

	// StaticMesh asset local space 기준의 선분. frame 제출 시 preview component world space로 변환한다.
	struct FTriangleCollisionPreviewLine
	{
		FVector Start;
		FVector End;
	};

private:
	FViewport* Viewport = nullptr;
	FWindowsWindow* Window = nullptr;
	FViewportRenderOptions RenderOptions;

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;
	UStaticMeshComponent* PreviewMeshComponent = nullptr;

	// 현재 edge cache가 어느 StaticMesh의 vertex/index를 기준으로 만들어졌는지 기억한다.
	// asset 포인터가 바뀌면 SubmitPreviewDebugDraw()에서 cache를 다시 생성한다.
	const FStaticMesh* CachedCollisionPreviewAsset = nullptr;
	TArray<FTriangleCollisionPreviewLine> TriangleCollisionPreviewLines;

	// StaticMesh Editor 인스턴스별 임시 UI 상태다. asset package에는 저장하지 않는다.
	bool bShowTriangleCollision = false;
	bool bTriangleCollisionOnly = false;

	bool bIsRenderable = false;

	FViewportCameraTransform ViewTransform;
	FRect ViewportScreenRect;

	FVector TargetLocation;
	bool bTargetLocationInitialized = false;
	FVector LastAppliedCameraLocation;
	bool bLastAppliedCameraLocationInitialized = false;
	const float SmoothLocationSpeed = 10.0f;
};
