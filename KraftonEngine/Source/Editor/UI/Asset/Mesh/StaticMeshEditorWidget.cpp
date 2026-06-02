#include "StaticMeshEditorWidget.h"

#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Mesh/MeshManager.h"
#include "Mesh/Static/StaticMesh.h"
#include "Mesh/Static/StaticMeshAsset.h"
#include "Runtime/Engine.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Viewport/Viewport.h"

#include <imgui.h>

namespace
{
	FString FormatStaticMeshStatCount(size_t Value)
	{
		FString Result = std::to_string(Value);
		for (int32 InsertPos = static_cast<int32>(Result.length()) - 3; InsertPos > 0; InsertPos -= 3)
		{
			Result.insert(static_cast<size_t>(InsertPos), ",");
		}
		return Result;
	}

	FString FormatStaticMeshByteSize(size_t Value)
	{
		if (Value >= 1024 * 1024)
		{
			return std::to_string(Value / (1024 * 1024)) + " MB";
		}
		if (Value >= 1024)
		{
			return std::to_string(Value / 1024) + " KB";
		}
		return std::to_string(Value) + " B";
	}
}

static uint32 GNextStaticMeshEditorInstanceId = 0;

FStaticMeshEditorWidget::FStaticMeshEditorWidget()
	: InstanceId(GNextStaticMeshEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("StaticMeshEditorPreview_" + Id);
	WindowIdSuffix = "###StaticMeshEditor_" + Id;
}

bool FStaticMeshEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UStaticMesh>();
}

bool FStaticMeshEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const UStaticMesh* CurrentMesh = Cast<UStaticMesh>(EditedObject);
	const UStaticMesh* RequestedMesh = Cast<UStaticMesh>(Object);
	if (!IsOpen() || !CurrentMesh || !RequestedMesh)
	{
		return false;
	}

	const FString& CurrentPath = CurrentMesh->GetAssetPathFileName();
	return !CurrentPath.empty()
		&& CurrentPath != "None"
		&& CurrentPath == RequestedMesh->GetAssetPathFileName();
}

void FStaticMeshEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	UStaticMeshComponent* PreviewComp = nullptr;
	if (UStaticMesh* Mesh = Cast<UStaticMesh>(EditedObject))
	{
		PreviewComp = Actor->AddComponent<UStaticMeshComponent>();
		PreviewComp->SetStaticMesh(Mesh);
		Actor->SetRootComponent(PreviewComp);
	}
	Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	const FBoundingBox Bounds = PreviewComp
		? PreviewComp->GetWorldBoundingBox()
		: FBoundingBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f));

	const FVector Extent = Bounds.GetExtent();
	const float FloorZ = Bounds.Min.Z - 0.02f;
	const float FloorScale = max(Extent.X, Extent.Y) * 10.0f;

	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>();
	LightComp->SetShadowBias(0.0f);
	LightComp->PushToScene();

	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	FloorActor->InitDefaultComponents("Content/Data/BasicShape/Cube.OBJ");
	FloorActor->SetActorLocation(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, FloorZ));
	FloorActor->SetActorScale(FVector(FloorScale, FloorScale, 0.02f));

	ImVec2 ViewportSize = ImGui::GetContentRegionAvail();

	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewMeshComponent(Actor->GetComponentByClass<UStaticMeshComponent>());
	ViewportClient.ResetCameraToPreviewBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);

	FSlateApplication::Get().RegisterViewport(&ViewportClient);
}

void FStaticMeshEditorWidget::Close()
{
	FAssetEditorWidget::Close();

	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		FScene& PreviewScene = PreviewWorld->GetScene();
		GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);

		if (PreviewWorldHandle.IsValid())
		{
			GEngine->DestroyWorldContext(PreviewWorldHandle);
		}
	}

	FSlateApplication::Get().UnregisterViewport(&ViewportClient);
	ViewportClient.Release();
}

void FStaticMeshEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}
}

void FStaticMeshEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FStaticMeshEditorViewportClient*>(&ViewportClient));
	}
}

void FStaticMeshEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	static float DetailsWidth = 300.0f;
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(EditedObject);

	bool bWindowOpen = true;
	FString VisibleTitle = "Static Mesh Editor";
	const FString AssetPath = StaticMesh ? StaticMesh->GetAssetPathFileName() : FString();
	if (!AssetPath.empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += AssetPath;
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;
	if (ViewportClient.IsMouseOverViewport())
	{
		WindowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
	}

	FString WindowTitle = VisibleTitle + WindowIdSuffix;
	ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_Once);
	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen, WindowFlags))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			Close();
		}
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);
	}

	ImGui::BeginGroup();
	{
		float AvailableWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		ImVec2 Size = ImVec2(AvailableWidth, ImGui::GetContentRegionAvail().y);

		ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
		ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

		FViewport* VP = ViewportClient.GetViewport();
		if (VP && Size.x > 0 && Size.y > 0)
		{
			VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));

			if (VP->GetSRV())
			{
				ImGui::Image((ImTextureID)VP->GetSRV(), Size);
				// ImGui 인지 hover 를 입력 소유권 중재에 보고.
				FSlateApplication::Get().SetViewportImGuiHovered(&ViewportClient, ImGui::IsItemHovered());
			}

			constexpr float ToolbarHeight = 28.0f;

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRectFilled(ViewportPos,
				ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ToolbarHeight),
				IM_COL32(40, 40, 40, 255));

			FViewportToolbarContext Context;
			Context.Renderer = &GEngine->GetRenderer();
			Context.Settings = &FEditorSettings::Get().MeshEditorViewportSettings;
			Context.RenderOptions = &ViewportClient.GetRenderOptions();
			Context.ToolbarLeft = ViewportPos.x;
			Context.ToolbarTop = ViewportPos.y;
			Context.ToolbarWidth = Size.x;
			Context.bReservePlayStopSpace = false;
			Context.bShowAddActor = false;
			Context.bShowGizmoControls = false;

			FViewportToolbar::Render(Context);
			RenderMeshStatsOverlay(DrawList, ViewportPos);
		}
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::BeginChild("Details", ImVec2(DetailsWidth, 0), true);
	ImGui::Text("Static Mesh Details");
	ImGui::Separator();
	RenderDetailsPanel(StaticMesh);
	ImGui::EndChild();

	ImGui::End();

	if (!bWindowOpen)
	{
		Close();
	}
}

void FStaticMeshEditorWidget::RenderMeshStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos) const
{
	if (!DrawList || !EditedObject)
	{
		return;
	}

	size_t VertexCount = 0;
	size_t TriangleCount = 0;

	if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(EditedObject))
	{
		if (const FStaticMesh* Asset = StaticMesh->GetStaticMeshAsset())
		{
			VertexCount = Asset->Vertices.size();
			TriangleCount = Asset->Indices.size() / 3;
		}
	}

	// preview texture 위에 항상 보이도록 ImGui window draw list에 직접 통계를 그린다.
	// 그림자를 한 번 먼저 찍어 밝은 mesh 위에서도 vertex/triangle 수를 읽기 쉽게 유지한다.
	const FString Text =
		"Triangles: " + FormatStaticMeshStatCount(TriangleCount) + "\n" +
		"Vertices: " + FormatStaticMeshStatCount(VertexCount);

	const ImVec2 TextPos(ViewportPos.x + 8.0f, ViewportPos.y + 36.0f);
	DrawList->AddText(ImVec2(TextPos.x + 1.0f, TextPos.y + 1.0f), IM_COL32(0, 0, 0, 220), Text.c_str());
	DrawList->AddText(TextPos, IM_COL32(235, 238, 242, 255), Text.c_str());
}

void FStaticMeshEditorWidget::RenderDetailsPanel(UStaticMesh* StaticMesh)
{
	FStaticMesh* Asset = StaticMesh ? StaticMesh->GetStaticMeshAsset() : nullptr;
	if (!Asset)
	{
		ImGui::TextDisabled("No static mesh data.");
		return;
	}

	ImGui::Text("Vertices: %s", FormatStaticMeshStatCount(Asset->Vertices.size()).c_str());
	ImGui::Text("Indices: %s", FormatStaticMeshStatCount(Asset->Indices.size()).c_str());
	ImGui::Text("Triangles: %s", FormatStaticMeshStatCount(Asset->Indices.size() / 3).c_str());
	ImGui::Text("Sections: %s", FormatStaticMeshStatCount(Asset->Sections.size()).c_str());

	ImGui::Spacing();
	ImGui::SeparatorText("Collision");
	ImGui::Text("Triangle Mesh: %s", StaticMesh->IsTriangleMeshCollisionEnabled() ? "Enabled" : "Disabled");
	if (const UBodySetup* BodySetup = StaticMesh->GetBodySetup())
	{
		// StaticMesh package 안에 저장된 PhysX cooked binary 크기다.
		// 대형 맵 collision이 asset 용량에 미치는 영향을 에디터에서 바로 확인할 수 있다.
		ImGui::Text("Cooked Data: %s", FormatStaticMeshByteSize(BodySetup->GetCookedTriangleMeshPhysXData().size()).c_str());
	}

	if (!StaticMesh->IsTriangleMeshCollisionEnabled())
	{
		// 렌더 메시를 import하는 것만으로는 BodySetup을 만들지 않는다.
		// 이 버튼을 누른 에셋만 PhysX triangle mesh binary를 cook하여 package에 저장한다.
		if (ImGui::Button("Build Triangle Collision", ImVec2(-1.0f, 0.0f)))
		{
			StaticMesh->SetTriangleMeshCollisionEnabled(true);
			if (!FMeshManager::SaveStaticMesh(StaticMesh))
			{
				MarkDirty();
			}
			else
			{
				// 처음 collision을 만든 직후에는 결과를 바로 검수할 수 있도록 wireframe도 함께 켠다.
				ViewportClient.SetShowTriangleCollision(true);
				ClearDirty();
			}
		}
	}
	else
	{
		// Show Collision은 저장되는 asset 설정이 아니라 현재 StaticMesh Editor preview의 표시 옵션이다.
		// 활성화하면 viewport client가 매 frame 원본 triangle edge를 EditorLines pass에 제출한다.
		bool bShowCollision = ViewportClient.IsShowingTriangleCollision();
		if (ImGui::Checkbox("Show Collision", &bShowCollision))
		{
			ViewportClient.SetShowTriangleCollision(bShowCollision);
		}

		// Collision Only는 wireframe이 렌더 메시 표면과 겹쳐 잘 보이지 않을 때 사용한다.
		// Show Collision이 꺼져 있으면 의미가 없으므로 UI도 비활성화한다.
		bool bCollisionOnly = ViewportClient.IsShowingTriangleCollisionOnly();
		if (!bShowCollision)
		{
			ImGui::BeginDisabled();
		}
		if (ImGui::Checkbox("Collision Only", &bCollisionOnly))
		{
			ViewportClient.SetTriangleCollisionOnly(bCollisionOnly);
		}
		if (!bShowCollision)
		{
			ImGui::EndDisabled();
		}

		// 원본 변경은 reimport가 자동으로 처리하지만, 에디터에서도 필요할 때 같은 데이터를 명시적으로 다시 cook할 수 있다.
		if (ImGui::Button("Rebuild Triangle Collision", ImVec2(-1.0f, 0.0f)))
		{
			if (!FMeshManager::SaveStaticMesh(StaticMesh))
			{
				MarkDirty();
			}
			else
			{
				ClearDirty();
			}
		}

		// collision을 제거하면 UStaticMesh가 BodySetup도 함께 제거한다.
		// 저장 후에는 이 렌더 메시가 physics 데이터를 소유하지 않는다.
		if (ImGui::Button("Remove Triangle Collision", ImVec2(-1.0f, 0.0f)))
		{
			ViewportClient.SetShowTriangleCollision(false);
			StaticMesh->SetTriangleMeshCollisionEnabled(false);
			if (!FMeshManager::SaveStaticMesh(StaticMesh))
			{
				MarkDirty();
			}
			else
			{
				ClearDirty();
			}
		}
	}
}
