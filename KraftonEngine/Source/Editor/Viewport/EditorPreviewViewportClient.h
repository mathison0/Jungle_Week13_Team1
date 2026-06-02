#pragma once

#include "Render/Types/POVProvider.h"
#include "Render/Types/ViewTypes.h"

class FViewport;
class FScene;
class UWorld;

class IEditorPreviewViewportClient : public IPOVProvider
{
public:
	virtual ~IEditorPreviewViewportClient() = default;

	virtual bool IsRenderable() const = 0;
	virtual bool IsMouseOverViewport() const = 0;

	virtual FViewport* GetViewport() const = 0;
	virtual UWorld* GetPreviewWorld() const = 0;

	virtual FViewportRenderOptions& GetRenderOptions() = 0;
	virtual const FViewportRenderOptions& GetRenderOptions() const = 0;

	virtual void NotifyViewportResized(int32 NewWidth, int32 NewHeight) = 0;

	// Preview viewport별 editor-only overlay 제출 지점.
	//
	// 일반 level viewport는 RenderCollector가 grid/debug draw를 수집하지만, asset preview viewport는
	// 가벼운 별도 렌더 경로를 사용한다. StaticMesh Editor처럼 preview 전용 wireframe이 필요한 경우
	// 이 hook에서 FScene의 ephemeral debug line을 채우면 현재 preview frame에만 표시할 수 있다.
	//
	// 기본 구현은 비어 있으므로 overlay가 필요 없는 Material/Particle preview에는 추가 비용이 없다.
	virtual void SubmitPreviewDebugDraw(FScene& Scene) { (void)Scene; }
};
