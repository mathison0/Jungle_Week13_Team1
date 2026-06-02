#include "MeshEditorWidget.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include "Mesh/Skeletal/SkeletalMesh.h"
#include "Mesh/Skeletal/SkeletalMeshAsset.h"
#include "Runtime/Engine.h"
#include "Component/Primitive/SkeletalMeshComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Shape/BoxComponent.h"
#include "Viewport/Viewport.h"
#include "GameFramework/World.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "Settings/EditorSettings.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Slate/SlateApplication.h"
#include "Input/InputSystem.h"
#include "Render/Shader/ShaderManager.h"
#include "Animation/Sequence/AnimSequence.h"
#include "Animation/Montage/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/Instance/AnimSingleNodeInstance.h"
#include "Animation/AnimationManager.h"
#include "Animation/Sequence/AnimDataModel.h"
#include "Asset/AssetRegistry.h"
#include "UI/Asset/Animation/AnimationTransportBar.h"
#include "UI/Asset/Animation/AnimationTimelinePanel.h"
#include "UI/Asset/Animation/AnimSequencePropertyPanel.h"
#include "UI/Asset/Animation/AnimMontagePropertyPanel.h"
#include "UI/Panel/EditorPropertyRenderer.h"
#include "UI/Util/EditorFileUtils.h"
#include "Editor/UI/Util/EditorTextureManager.h"
#include "Platform/Paths.h"
#include "Object/Object.h"
#include "Object/Reflection/UStruct.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetManager.h"
#include "Physics/PhysicsGeometry.h"
#include "Physics/IPhysicsScene.h"
#include "Mesh/MeshManager.h"
#include "Serialization/MemoryArchive.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

// Paths.h가 끌어오는 Windows.h는 GetCurrentTime을 GetTickCount로 치환한다.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

namespace
{
	ID3D11ShaderResourceView* LoadTabIcon(const wchar_t* FileName)
	{
		const FString Path = FPaths::ToUtf8(
			FPaths::Combine(FPaths::AssetDir(), L"Editor/ToolIcons/", FileName));
		return FEditorTextureManager::Get().GetOrLoadIcon(Path);
	}

	ID3D11ShaderResourceView* LoadEditorIcon(const wchar_t* FileName)
	{
		const FString Path = FPaths::ToUtf8(
			FPaths::Combine(FPaths::AssetDir(), L"Editor/Icons/", FileName));
		return FEditorTextureManager::Get().GetOrLoadIcon(Path);
	}

	bool IsSameProjectPath(const FString& A, const FString& B)
	{
		return FPaths::MakeProjectRelative(A) == FPaths::MakeProjectRelative(B);
	}

	bool ProjectRegularFileExists(const FString& Path)
	{
		std::filesystem::path FullPath(FPaths::ToWide(Path));
		if (!FullPath.is_absolute())
		{
			FullPath = std::filesystem::path(FPaths::RootDir()) / FullPath;
		}

		return std::filesystem::exists(FullPath) && std::filesystem::is_regular_file(FullPath);
	}

	FString GetExpectedSkeletalMeshPathForPhysicsAsset(const FString& PhysicsAssetPath)
	{
		std::filesystem::path MeshPath(FPaths::ToWide(FPaths::MakeProjectRelative(PhysicsAssetPath)));
		std::wstring Stem = MeshPath.stem().wstring();
		static constexpr const wchar_t* PhysicsAssetSuffix = L"_PhysicsAsset";

		// Importer가 만든 기본 짝 이름: Foo_PhysicsAsset.uasset -> Foo_SkeletalMesh.uasset.
		if (Stem.size() >= std::char_traits<wchar_t>::length(PhysicsAssetSuffix)
			&& Stem.compare(
				Stem.size() - std::char_traits<wchar_t>::length(PhysicsAssetSuffix),
				std::char_traits<wchar_t>::length(PhysicsAssetSuffix),
				PhysicsAssetSuffix) == 0)
		{
			Stem.resize(Stem.size() - std::char_traits<wchar_t>::length(PhysicsAssetSuffix));
		}

		MeshPath.replace_filename(Stem + L"_SkeletalMesh.uasset");
		return FPaths::ToUtf8(MeshPath.generic_wstring());
	}

	bool DoesMeshReferencePhysicsAsset(USkeletalMesh* Mesh, const FString& PhysicsAssetPath)
	{
		if (!Mesh)
		{
			return false;
		}

		if (IsSameProjectPath(Mesh->GetPhysicsAssetPath(), PhysicsAssetPath))
		{
			return true;
		}

		UPhysicsAsset* MeshPhysicsAsset = Mesh->GetPhysicsAsset();
		return MeshPhysicsAsset && IsSameProjectPath(MeshPhysicsAsset->GetAssetPathFileName(), PhysicsAssetPath);
	}

	USkeletalMesh* ResolveSkeletalMeshForPhysicsAsset(UPhysicsAsset* PhysicsAsset, ID3D11Device* Device)
	{
		if (!PhysicsAsset || !Device)
		{
			return nullptr;
		}

		const FString PhysicsAssetPath = FPaths::MakeProjectRelative(PhysicsAsset->GetAssetPathFileName());
		if (PhysicsAssetPath.empty() || PhysicsAssetPath == "None")
		{
			return nullptr;
		}

		// 대부분의 auto-generated PhysicsAsset은 같은 디렉터리의 SkeletalMesh와 이름으로 짝지어진다.
		const FString ExpectedMeshPath = GetExpectedSkeletalMeshPathForPhysicsAsset(PhysicsAssetPath);
		if (ProjectRegularFileExists(ExpectedMeshPath) && FMeshManager::IsSkeletalMeshPackage(ExpectedMeshPath))
		{
			if (USkeletalMesh* Mesh = FMeshManager::LoadSkeletalMesh(ExpectedMeshPath, Device))
			{
				if (!DoesMeshReferencePhysicsAsset(Mesh, PhysicsAssetPath))
				{
					Mesh->SetPhysicsAsset(PhysicsAsset);
				}
				return Mesh;
			}
		}

		// 이름 규칙이 깨진 경우에는 로드 가능한 SkeletalMesh 중 PhysicsAsset 참조가 일치하는 것을 찾는다.
		FMeshManager::ScanMeshAssets();
		for (const FAssetListItem& Item : FMeshManager::GetAvailableSkeletalMeshFiles())
		{
			USkeletalMesh* Mesh = FMeshManager::LoadSkeletalMesh(Item.FullPath, Device);
			if (DoesMeshReferencePhysicsAsset(Mesh, PhysicsAssetPath))
			{
				return Mesh;
			}
		}

		return nullptr;
	}



	FString FormatMeshStatCount(size_t Value)
	{
		FString Result = std::to_string(Value);
		for (int32 InsertPos = static_cast<int32>(Result.length()) - 3; InsertPos > 0; InsertPos -= 3)
		{
			Result.insert(static_cast<size_t>(InsertPos), ",");
		}
		return Result;
	}

	FString FormatMeshStatSeconds(double Seconds)
	{
		char Buffer[64] = {};
		std::snprintf(Buffer, sizeof(Buffer), "%.3f sec", Seconds);
		return FString(Buffer);
	}



	TMap<FString, double> GMeshImportDurationsByAssetPath;

	double GetRecordedImportDurationSeconds(const USkeletalMesh* Mesh)
	{
		if (!Mesh)
		{
			return -1.0;
		}

		const FString& AssetPath = Mesh->GetAssetPathFileName();
		if (AssetPath.empty() || AssetPath == "None")
		{
			return -1.0;
		}

		auto It = GMeshImportDurationsByAssetPath.find(AssetPath);
		return It != GMeshImportDurationsByAssetPath.end() ? It->second : -1.0;
	}

	FMorphTargetCurve& FindOrAddMorphCurve(UAnimSequence* Seq, const FString& MorphTargetName)
	{
		TArray<FMorphTargetCurve>& Curves = Seq->GetMutableMorphTargetCurves();
		for (FMorphTargetCurve& Curve : Curves)
		{
			if (Curve.MorphTargetName == MorphTargetName)
			{
				return Curve;
			}
		}
		FMorphTargetCurve NewCurve;
		NewCurve.MorphTargetName = MorphTargetName;
		Curves.push_back(std::move(NewCurve));
		return Curves.back();
	}

	void AddOrUpdateMorphCurveKey(FMorphTargetCurve& Curve, float TimeSeconds, float Value)
	{
		constexpr float TimeTolerance = 1.0e-4f;
		for (FRawFloatCurveKey& Key : Curve.Curve.Keys)
		{
			if (std::fabs(Key.TimeSeconds - TimeSeconds) <= TimeTolerance)
			{
				Key.Value = Value;
				return;
			}
		}
		FRawFloatCurveKey NewKey;
		NewKey.TimeSeconds = TimeSeconds;
		NewKey.Value = Value;
		NewKey.Interpolation = 2;
		Curve.Curve.Keys.push_back(NewKey);
		std::sort(
			Curve.Curve.Keys.begin(),
			Curve.Curve.Keys.end(),
			[](const FRawFloatCurveKey& A, const FRawFloatCurveKey& B)
			{
				return A.TimeSeconds < B.TimeSeconds;
			}
		);
	}

	EUberLitDefines::ELightingModel GetLightingModelForViewMode(EViewMode ViewMode)
	{
		switch (ViewMode)
		{
		case EViewMode::Unlit:       return EUberLitDefines::ELightingModel::Unlit;
		case EViewMode::Lit_Gouraud: return EUberLitDefines::ELightingModel::Gouraud;
		case EViewMode::Lit_Lambert: return EUberLitDefines::ELightingModel::Lambert;
		case EViewMode::Lit_Phong:
		case EViewMode::LightCulling:
		default:                     return EUberLitDefines::ELightingModel::Phong;
		}
	}
}

static uint32 GNextMeshEditorInstanceId = 0;

void FMeshEditorWidget::RecordImportDurationForAsset(const FString& AssetPath, double Seconds)
{
	if (AssetPath.empty() || AssetPath == "None" || Seconds < 0.0)
	{
		return;
	}

	GMeshImportDurationsByAssetPath[AssetPath] = Seconds;
}

void FMeshEditorWidget::ClearImportDurationForAsset(const FString& AssetPath)
{
	if (AssetPath.empty() || AssetPath == "None")
	{
		return;
	}

	GMeshImportDurationsByAssetPath.erase(AssetPath);
}

FMeshEditorWidget::FMeshEditorWidget()
	: InstanceId(GNextMeshEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("MeshEditorPreview_" + Id);
	WindowIdSuffix = "###MeshEditor_" + Id;
}

bool FMeshEditorWidget::CanEdit(UObject* Object) const
{
	return Object && (Object->IsA<USkeletalMesh>() || Object->IsA<UPhysicsAsset>());
}

bool FMeshEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const USkeletalMesh* CurrentMesh = Cast<USkeletalMesh>(EditedObject);
	const USkeletalMesh* RequestedMesh = Cast<USkeletalMesh>(Object);
	if (!IsOpen() || !CurrentMesh)
	{
		return false;
	}

	const UPhysicsAsset* RequestedPhysicsAsset = Cast<UPhysicsAsset>(Object);
	if (RequestedPhysicsAsset)
	{
		return DoesMeshReferencePhysicsAsset(
			const_cast<USkeletalMesh*>(CurrentMesh),
			RequestedPhysicsAsset->GetAssetPathFileName());
	}

	if (!RequestedMesh)
	{
		return false;
	}

	const FString& CurrentPath = CurrentMesh->GetAssetPathFileName();
	return !CurrentPath.empty()
		&& CurrentPath != "None"
		&& CurrentPath == RequestedMesh->GetAssetPathFileName();
}

void FMeshEditorWidget::Open(UObject* Object)
{
	bool bOpenPhysicalAssetTab = false;
	if (UPhysicsAsset* PhysicsAsset = Cast<UPhysicsAsset>(Object))
	{
		// Physical Asset 편집 UI는 SkeletalMesh 프리뷰/본 계층을 필요로 하므로 연결된 Mesh를 연다.
		Object = ResolveSkeletalMeshForPhysicsAsset(
			PhysicsAsset,
			GEngine->GetRenderer().GetFD3DDevice().GetDevice());
		bOpenPhysicalAssetTab = Object != nullptr;
	}

	if (!Object)
	{
		return;
	}

	FAssetEditorWidget::Open(Object);

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(EditedObject))
	{
		USkeletalMeshComponent* Comp = Actor->AddComponent<USkeletalMeshComponent>();
		Comp->SetSkeletalMesh(Mesh);
		Actor->SetRootComponent(Comp);
	}
	Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>();
	LightComp->SetShadowBias(0.0f);
	LightComp->PushToScene();

	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	FloorActor->InitDefaultComponents("Content/Data/BasicShape/Cube.OBJ");
	FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -1.f));
	FloorActor->SetActorScale(FVector(10.0f, 10.0f, 1.f));
	if (UBoxComponent* FloorCollider = FloorActor->AddComponent<UBoxComponent>())
	{
		FloorCollider->AttachToComponent(FloorActor->GetRootComponent());
		FloorCollider->SetBoxExtent(FVector(1.0f, 1.0f, 1.0f));
		FloorCollider->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
		FloorCollider->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorCollider->SetCollisionObjectType(ECollisionChannel::WorldStatic);
		FloorCollider->SetCollisionResponseToAllChannels(ECollisionResponse::Block);
		FloorCollider->SetSimulatePhysics(false);
		FloorCollider->SetVisibility(false);
	}

	ImVec2 ViewportSize = ImGui::GetContentRegionAvail();

	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewMeshComponent(Actor->GetComponentByClass<USkeletalMeshComponent>());

	ViewportClient.CreatePreviewGizmo();
	ViewportClient.CreateBoneDebugComponent();
	ViewportClient.ResetCameraToPreviousBounds();
	ViewportClient.SetOnPhysicsBodyPicked([this](int32 BoneIndex, UBodySetup* BodySetup)
		{
			SelectedBoneIndex = BoneIndex;
			SelectedBodySetup = BodySetup;
			PhysicsGraphFocusBodySetup = BodySetup;
			SelectedConstraintIndex = -1;
		});
	ViewportClient.SetOnPhysicsConstraintPicked([this](int32 ConstraintIndex)
		{
			USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
			UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
			FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
			if (!PhysAsset || !Asset || ConstraintIndex < 0 || ConstraintIndex >= static_cast<int32>(PhysAsset->ConstraintSetups.size()))
			{
				return;
			}

			const FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[ConstraintIndex];
			SelectedConstraintIndex = ConstraintIndex;
			SelectedBodySetup = nullptr;
			PhysicsGraphFocusBodySetup = PhysAsset->FindBodySetupByBoneName(Constraint.ChildBoneName);
			SelectedBoneIndex = -1;

			const FString ChildBoneName = Constraint.ChildBoneName.ToString();
			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
			{
				if (Asset->Bones[BoneIndex].Name == ChildBoneName)
				{
					SelectedBoneIndex = BoneIndex;
					break;
				}
			}
		});
	ViewportClient.SetOnPhysicsAssetPickMissed([this]()
		{
			SelectedBoneIndex = -1;
			SelectedBodySetup = nullptr;
			PhysicsGraphFocusBodySetup = nullptr;
			SelectedConstraintIndex = -1;
		});
	ViewportClient.SetOnPhysicsAssetModified([this]()
		{
			MarkDirty();
			ViewportClient.RefreshPhysicsAssetDebugDraw();
		});

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);

	ViewportClient.SetSelectedBone(Cast<USkeletalMesh>(EditedObject), -1);

	FSlateApplication::Get().RegisterViewport(&ViewportClient);

	// 디스크의 기존 AnimSequence .uasset 들을 목록에 채워 둔다(런타임 Load/Save 만으론 안 잡힘).
	FAnimationManager::Get().RefreshAvailableAnimations();

	ActiveTab = bOpenPhysicalAssetTab ? EMeshEditorTab::PhysicalAsset : EMeshEditorTab::Skeleton;
	AnimTabState = FAnimationTabState{};
	SelectedBoneIndex = -1;
	SelectedBodySetup = nullptr;
	PhysicsGraphFocusBodySetup = nullptr;
	SelectedConstraintIndex = -1;
	PhysicsGraphNodePositions.clear();
	CapturePhysicsAssetBaseline();
}

void FMeshEditorWidget::FocusObject(UObject* Object)
{
	if (Object && Object->IsA<UPhysicsAsset>())
	{
		// 같은 Mesh editor가 이미 열려 있으면 PhysicsAsset 탭만 앞으로 가져온다.
		ActiveTab = EMeshEditorTab::PhysicalAsset;
	}
	RequestFocus();
}

void FMeshEditorWidget::CapturePhysicsAssetBaseline()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;

	PhysicsAssetBaselineAsset = PhysAsset;
	PhysicsAssetBaselineSnapshot.clear();
	bPhysicsAssetBaselineHadAsset = PhysAsset != nullptr;

	if (!PhysAsset)
	{
		return;
	}

	FMemoryArchive Writer(/*bInIsSaving=*/true);
	PhysAsset->Serialize(Writer);
	PhysicsAssetBaselineSnapshot = Writer.GetBuffer();
}

void FMeshEditorWidget::RestorePhysicsAssetBaseline()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	if (!SkeletalMesh)
	{
		return;
	}

	StopPhysicsAssetSimulation(true);

	UPhysicsAsset* CurrentPhysAsset = SkeletalMesh->GetPhysicsAsset();
	if (!bPhysicsAssetBaselineHadAsset)
	{
		if (CurrentPhysAsset)
		{
			for (UBodySetup* BodySetup : CurrentPhysAsset->BodySetups)
			{
				if (BodySetup)
				{
					UObjectManager::Get().DestroyObject(BodySetup);
				}
			}
			CurrentPhysAsset->BodySetups.clear();
			CurrentPhysAsset->ConstraintSetups.clear();

			if (CurrentPhysAsset->GetAssetPathFileName().empty() || CurrentPhysAsset->GetAssetPathFileName() == "None")
			{
				UObjectManager::Get().DestroyObject(CurrentPhysAsset);
			}
		}

		SkeletalMesh->SetPhysicsAsset(nullptr);
	}
	else if (PhysicsAssetBaselineAsset && !PhysicsAssetBaselineSnapshot.empty())
	{
		if (CurrentPhysAsset && CurrentPhysAsset != PhysicsAssetBaselineAsset
			&& (CurrentPhysAsset->GetAssetPathFileName().empty() || CurrentPhysAsset->GetAssetPathFileName() == "None"))
		{
			for (UBodySetup* BodySetup : CurrentPhysAsset->BodySetups)
			{
				if (BodySetup)
				{
					UObjectManager::Get().DestroyObject(BodySetup);
				}
			}
			UObjectManager::Get().DestroyObject(CurrentPhysAsset);
		}

		for (UBodySetup* BodySetup : PhysicsAssetBaselineAsset->BodySetups)
		{
			if (BodySetup)
			{
				UObjectManager::Get().DestroyObject(BodySetup);
			}
		}
		PhysicsAssetBaselineAsset->BodySetups.clear();
		PhysicsAssetBaselineAsset->ConstraintSetups.clear();

		FMemoryArchive Reader(PhysicsAssetBaselineSnapshot, /*bInIsSaving=*/false);
		PhysicsAssetBaselineAsset->Serialize(Reader);
		SkeletalMesh->SetPhysicsAsset(PhysicsAssetBaselineAsset);
	}

	SelectedBodySetup = nullptr;
	PhysicsGraphFocusBodySetup = nullptr;
	SelectedConstraintIndex = -1;
	PhysicsGraphNodePositions.clear();
	ViewportClient.SetSelectedBone(SkeletalMesh, -1);
	ClearDirty();
}

bool FMeshEditorWidget::SaveCurrentPhysicsAsset()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
	if (!SkeletalMesh || !PhysAsset)
	{
		return false;
	}

	const FString MeshPackagePath = SkeletalMesh->GetAssetPathFileName();
	FString PhysicsAssetPath = PhysAsset->GetAssetPathFileName();
	if ((PhysicsAssetPath.empty() || PhysicsAssetPath == "None")
		&& !MeshPackagePath.empty() && MeshPackagePath != "None")
	{
		PhysicsAssetPath = FPhysicsAssetManager::GetPhysicsAssetPackagePath(MeshPackagePath);
		PhysAsset->SetAssetPathFileName(PhysicsAssetPath);
		SkeletalMesh->SetPhysicsAsset(PhysAsset);
	}

	const FSkeletalMesh* MeshAsset = SkeletalMesh->GetSkeletalMeshAsset();
	const FString SourcePath = MeshAsset ? MeshAsset->PathFileName : FString();
	if (!FPhysicsAssetManager::Get().Save(PhysAsset, SourcePath)
		|| !FMeshManager::SaveSkeletalMesh(SkeletalMesh))
	{
		return false;
	}

	ClearDirty();
	CapturePhysicsAssetBaseline();
	return true;
}

bool FMeshEditorWidget::HasUnsavedPhysicsAssetChanges() const
{
	const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	const UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
	return IsDirty() && (PhysAsset || bPhysicsAssetBaselineHadAsset);
}

void FMeshEditorWidget::RequestClose()
{
	if (bPhysicsAssetCloseConfirmOpen)
	{
		return;
	}

	if (HasUnsavedPhysicsAssetChanges())
	{
		bPhysicsAssetCloseConfirmOpen = true;
		RequestFocus();
		return;
	}

	bPendingClose = true;
}

void FMeshEditorWidget::RenderUnsavedPhysicsAssetModal()
{
	if (!bPhysicsAssetCloseConfirmOpen)
	{
		return;
	}

	const char* PopupId = "Unsaved Physics Asset Changes";
	ImGui::OpenPopup(PopupId);
	if (ImGui::BeginPopupModal(PopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Physics Asset has unsaved changes.");
		ImGui::TextUnformatted("Save changes before closing?");
		ImGui::Dummy(ImVec2(0.0f, 8.0f));

		if (ImGui::Button("Save", ImVec2(90.0f, 0.0f)))
		{
			if (SaveCurrentPhysicsAsset())
			{
				bPhysicsAssetCloseConfirmOpen = false;
				bPendingClose = true;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard", ImVec2(90.0f, 0.0f)))
		{
			RestorePhysicsAssetBaseline();
			bPhysicsAssetCloseConfirmOpen = false;
			bPendingClose = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
		{
			bPhysicsAssetCloseConfirmOpen = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void FMeshEditorWidget::Close()
{
	if (!bPendingClose && HasUnsavedPhysicsAssetChanges())
	{
		RequestClose();
		return;
	}

	StopPhysicsAssetSimulation(false);

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
	ViewportClient.SetOnPhysicsBodyPicked(nullptr);
	ViewportClient.SetOnPhysicsConstraintPicked(nullptr);
	ViewportClient.SetOnPhysicsAssetPickMissed(nullptr);
	ViewportClient.SetOnPhysicsAssetModified(nullptr);
	SelectedBodySetup = nullptr;
	PhysicsGraphFocusBodySetup = nullptr;
	SelectedConstraintIndex = -1;
	PhysicsGraphNodePositions.clear();
	bPhysicsAssetSimulationRunning = false;
	PhysicsAssetBaselineAsset = nullptr;
	PhysicsAssetBaselineSnapshot.clear();
	bPhysicsAssetBaselineHadAsset = false;
	bPhysicsAssetCloseConfirmOpen = false;
}

// ViewportClient의 Tick에서 애니메이션 업데이트와 물리 시뮬레이션을 처리한다. 애니메이션 탭이 활성화된 경우에만 애니메이션을 업데이트한다.
void FMeshEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}

	// Ragdoll 시뮬레이션이 돌고 있으면 매 프레임 물리 시뮬레이션을 진행한다. 시뮬레이션이 끝났는지는 컴포넌트가 여전히 Ragdoll 시뮬레이션 중인지 여부로 판단한다.
	if (bPhysicsAssetSimulationRunning)
	{
		USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
		if (!Comp || !Comp->IsRagdollSimulating())
		{
			bPhysicsAssetSimulationRunning = false;
			return;
		}

		if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
		{
			if (IPhysicsScene* PhysicsScene = PreviewWorld->GetPhysicsScene())
			{
				const float SimDeltaTime = DeltaTime * std::clamp(PhysicsAssetSimulationTimeScale, 0.0f, 2.0f);
				PhysicsScene->Tick(SimDeltaTime);
			}
		}
		return;
	}

	// 애니메이션 탭이 활성화된 경우에만 애니메이션을 업데이트한다. 다른 탭에서는 애니메이션이 업데이트되지 않으므로, 애니메이션이 적용된 포즈로 프리뷰 메시가 렌더링되지 않는다.
	if (ActiveTab == EMeshEditorTab::Animation)
	{
		USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
		if (!Comp) return;
		UAnimSingleNodeInstance* NodeInst = Comp->GetAnimNodeInstance(FName::None);
		if (!NodeInst) return;

		NodeInst->UpdateAnimation(DeltaTime);

		USkeletalMesh* Mesh = Comp->GetSkeletalMesh();
		if (!Mesh) return;
		FSkeletalMesh* Asset = Mesh->GetSkeletalMeshAsset();
		if (!Asset || Asset->Bones.empty()) return;

		FPoseContext Out;
		Out.SkeletalMesh = Mesh;
		Out.Pose.resize(Asset->Bones.size());
		Out.ResetToRefPose();

		NodeInst->EvaluatePose(Out);
		ApplyMorphPreviewOverrides(Out.MorphWeights);

		Comp->SetAnimationPose(Out.Pose, Out.MorphWeights);
	}
}

// Ragdoll 시뮬레이션을 시작한다. 이미 시뮬레이션이 돌고 있으면 아무 작업도 하지 않는다.
void FMeshEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FMeshEditorViewportClient*>(&ViewportClient));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Render entry point
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::Render(float DeltaTime)
{
	// 1프레임 지연 close (SRV lifetime issue)
	if (bPendingClose)
	{
		Close();
		bPendingClose = false;
		return;
	}
	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	if (bPhysicsGraphCapturingMouse)
	{
		ImGui::SetNextFrameWantCaptureMouse(true);
		InputSystem::Get().SetGuiMouseCapture(true);
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
		{
			bPhysicsGraphCapturingMouse = false;
		}
	}

	bool bWindowOpen = true;
	FString VisibleTitle = "Mesh Editor";
	const FString AssetPath = SkeletalMesh ? SkeletalMesh->GetAssetPathFileName() : FString();
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
	if (ViewportClient.IsMouseOverViewport() || bPhysicsGraphCapturingMouse)
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
		// 접힌 동안엔 hover 를 보고하지 않음
		ImGui::End();
		if (!bWindowOpen)
		{
			RequestClose();
		}
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);
	}

	RenderTabBar();
	ImGui::Separator();
	// Physical Asset 탭에서만 preview용 PhysicsAsset 디버그 렌더링을 켭니다.
	ViewportClient.SetPhysicsAssetDebugDrawEnabled(ActiveTab == EMeshEditorTab::PhysicalAsset);
	ViewportClient.SetPhysicsAssetBodyShowMode(ActiveTab == EMeshEditorTab::PhysicalAsset
		? ViewportClient.GetRenderOptions().PhysicsAssetBodyShowMode
		: EPhysicsAssetBodyShowMode::None);
	ViewportClient.SetPhysicsAssetConstraintShowMode(ActiveTab == EMeshEditorTab::PhysicalAsset
		? ViewportClient.GetRenderOptions().PhysicsAssetConstraintShowMode
		: EPhysicsAssetConstraintShowMode::None);
	ViewportClient.SetSelectedPhysicsConstraintIndex(ActiveTab == EMeshEditorTab::PhysicalAsset ? SelectedConstraintIndex : -1);

	const float AvailableHeight = ImGui::GetContentRegionAvail().y;

	switch (ActiveTab)
	{
	case EMeshEditorTab::Skeleton:
		RenderSkeletonLayout();
		break;
	case EMeshEditorTab::Mesh:
		RenderMeshLayout();
		break;
	case EMeshEditorTab::Animation:
		RenderAnimationLayout(AvailableHeight);
		break;
	case EMeshEditorTab::PhysicalAsset:
		RenderPhysicalAssetLayout();
		break;
	}

	RenderUnsavedPhysicsAssetModal();

	ImGui::End();

	if (!bWindowOpen)
	{
		RequestClose();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tab bar
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderTabBar()
{
	// 언리얼 Persona 모드 툴바: 평평한 버튼 + 선택 시 액센트 밑줄.
	constexpr float BarHeight = 30.0f;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2    BarPos = ImGui::GetCursorScreenPos();
	const float     BarWidth = ImGui::GetContentRegionAvail().x;
	DrawList->AddRectFilled(BarPos, ImVec2(BarPos.x + BarWidth, BarPos.y + BarHeight),
		IM_COL32(38, 38, 38, 255));

	auto TabButton = [&](const char* Label, const wchar_t* IconFile, EMeshEditorTab Tab)
		{
			const bool      bActive = (ActiveTab == Tab);
			constexpr float IconSz = 18.0f;
			constexpr float PadX = 14.0f;
			constexpr float Gap = 8.0f;

			const ImVec2 Pos = ImGui::GetCursorScreenPos();
			const ImVec2 TextSz = ImGui::CalcTextSize(Label);
			const float  Width = PadX + IconSz + Gap + TextSz.x + PadX;

			ImGui::InvisibleButton(Label, ImVec2(Width, BarHeight));
			const bool bHovered = ImGui::IsItemHovered();
			if (ImGui::IsItemClicked())
			{
				const EMeshEditorTab PreviousTab = ActiveTab;
				ActiveTab = Tab;
				if (PreviousTab == EMeshEditorTab::PhysicalAsset && ActiveTab != EMeshEditorTab::PhysicalAsset)
				{
					StopPhysicsAssetSimulation(true);
				}
				if (PreviousTab != ActiveTab && ActiveTab == EMeshEditorTab::Skeleton)
				{
					if (USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent())
					{
						Comp->ApplyBoneEditBasePose();
					}
				}
			}

			if (bActive || bHovered)
			{
				DrawList->AddRectFilled(Pos, ImVec2(Pos.x + Width, Pos.y + BarHeight),
					bActive ? IM_COL32(41, 41, 41, 255) : IM_COL32(255, 255, 255, 20));
			}

			const float IconY = Pos.y + (BarHeight - IconSz) * 0.5f;
			if (ID3D11ShaderResourceView* Icon = LoadTabIcon(IconFile))
			{
				DrawList->AddImage(reinterpret_cast<ImTextureID>(Icon),
					ImVec2(Pos.x + PadX, IconY),
					ImVec2(Pos.x + PadX + IconSz, IconY + IconSz));
			}

			DrawList->AddText(ImVec2(Pos.x + PadX + IconSz + Gap, Pos.y + (BarHeight - TextSz.y) * 0.5f),
				bActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 190, 190, 255),
				Label);

			if (bActive)
			{
				DrawList->AddRectFilled(ImVec2(Pos.x, Pos.y + BarHeight - 2.0f),
					ImVec2(Pos.x + Width, Pos.y + BarHeight),
					IM_COL32(64, 132, 224, 255));
			}
			ImGui::SameLine(0.0f, 0.0f);
		};

	TabButton("Skeleton", L"Skeleton.png", EMeshEditorTab::Skeleton);
	TabButton("Mesh", L"SkeletalMesh.png", EMeshEditorTab::Mesh);
	TabButton("Animation", L"Animation.png", EMeshEditorTab::Animation);
	TabButton("Physical Asset", L"Physics.png", EMeshEditorTab::PhysicalAsset);

	ImGui::NewLine();
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared: viewport panel
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderViewportPanel(ImVec2 Size)
{
	ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
	ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

	FViewport* VP = ViewportClient.GetViewport();
	if (!VP || Size.x <= 0 || Size.y <= 0)
	{
		ImGui::Dummy(Size);
		return;
	}

	VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));

	// Mesh editor viewport의 렌더 타겟을 ImGui 이미지로 붙입니다.
	if (VP->GetSRV())
	{
		ImGui::Image((ImTextureID)VP->GetSRV(), Size);
	}
	else
	{
		ImGui::Dummy(Size);
	}

	// ImGui가 계산한 hover(다른 창에 가려지면 false)를 입력 소유권 중재에 보고.
	FSlateApplication::Get().SetViewportImGuiHovered(&ViewportClient, ImGui::IsItemHovered());

	constexpr float ToolbarHeight = 28.0f;
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(ViewportPos, ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ToolbarHeight), IM_COL32(40, 40, 40, 255));

	FViewportToolbarContext Context;
	Context.Renderer = &GEngine->GetRenderer();
	Context.Gizmo = ViewportClient.GetGizmo();
	Context.Settings = &FEditorSettings::Get().MeshEditorViewportSettings;
	Context.RenderOptions = &ViewportClient.GetRenderOptions();
	Context.ToolbarLeft = ViewportPos.x;
	Context.ToolbarTop = ViewportPos.y;
	Context.ToolbarWidth = Size.x;
	Context.bReservePlayStopSpace = false;
	Context.bShowAddActor = false;
	Context.OnCoordSystemToggled = [&]()
		{
			FGizmoToolSettings& Settings = FEditorSettings::Get().MeshEditorViewportSettings.Gizmo;
			Settings.CoordSystem = (Settings.CoordSystem == EEditorCoordSystem::World) ? EEditorCoordSystem::Local : EEditorCoordSystem::World;
			ViewportClient.ApplyTransformSettingsToGizmo();
		};
	Context.OnSettingsChanged = [&]()
		{
			ViewportClient.ApplyTransformSettingsToGizmo();
		};
	Context.OnRenderViewModeExtras = [&]()
		{
			const EBoneDebugDrawMode CurrentBoneDrawMode = ViewportClient.GetBoneDebugDrawMode();
			int32                    BoneDrawMode = static_cast<int32>(CurrentBoneDrawMode);
			ImGui::Text("Bone Display");
			ImGui::RadioButton("Selected Bone", &BoneDrawMode, static_cast<int32>(EBoneDebugDrawMode::SelectedOnly));
			ImGui::RadioButton("All Bones", &BoneDrawMode, static_cast<int32>(EBoneDebugDrawMode::AllBones));
			if (BoneDrawMode != static_cast<int32>(CurrentBoneDrawMode))
			{
				ViewportClient.SetBoneDebugDrawMode(static_cast<EBoneDebugDrawMode>(BoneDrawMode));
			}

			FViewportRenderOptions& RenderOptions = ViewportClient.GetRenderOptions();
			bool bWeightBoneHeatMap = RenderOptions.bWeightBoneHeatMap;
			if (ImGui::Checkbox("Weight Bone HeatMap", &bWeightBoneHeatMap))
			{
				RenderOptions.bWeightBoneHeatMap = bWeightBoneHeatMap;
				RenderOptions.WeightBoneHeatMapBoneIndex = SelectedBoneIndex;
				if (bWeightBoneHeatMap)
				{
					FShaderManager::Get().GetOrCreateUberLitPermutation(
						GetLightingModelForViewMode(RenderOptions.ViewMode),
						EUberLitDefines::EVertexFactory::SkeletalMesh,
						EShaderErrorMode::Notification,
						true);
				}
			}

			if (ActiveTab == EMeshEditorTab::PhysicalAsset)
			{
				// PhysicsAsset body 표시 방식은 preview용 BoneDebugComponent로 전달됩니다.
				const char* BodyShowItems[] = { "Solid", "Wireframe", "None" };
				int32 BodyShowMode = static_cast<int32>(RenderOptions.PhysicsAssetBodyShowMode);
				if (ImGui::Combo("Physics Body", &BodyShowMode, BodyShowItems, IM_ARRAYSIZE(BodyShowItems)))
				{
					RenderOptions.PhysicsAssetBodyShowMode = static_cast<EPhysicsAssetBodyShowMode>(BodyShowMode);
				}

				// Constraint 표시는 solid limit 시각화만 지원합니다.
				const char* ConstraintShowItems[] = { "Solid", "None" };
				int32 ConstraintShowMode = static_cast<int32>(RenderOptions.PhysicsAssetConstraintShowMode);
				if (ImGui::Combo("Physics Constraint", &ConstraintShowMode, ConstraintShowItems, IM_ARRAYSIZE(ConstraintShowItems)))
				{
					RenderOptions.PhysicsAssetConstraintShowMode = static_cast<EPhysicsAssetConstraintShowMode>(ConstraintShowMode);
				}
			}
		};

	FViewportToolbar::Render(Context);
	RenderMeshStatsOverlay(DrawList, ViewportPos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Skeleton tab
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderSkeletonLayout()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);

	// Left: bone hierarchy
	ImGui::BeginChild("BoneHierarchy", ImVec2(HierarchyWidth, 0), true);
	ImGui::Text("Bone Hierarchy");
	ImGui::Separator();
	if (SkeletalMesh)
	{
		const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		if (Asset)
		{
			for (int32 i = 0; i < static_cast<int32>(Asset->Bones.size()); ++i)
			{
				if (Asset->Bones[i].ParentIndex == -1)
				{
					RenderBoneTree(Asset, i);
				}
			}
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Splitter
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::Button("##skelSplitter", ImVec2(4.0f, -1.0f));
	if (ImGui::IsItemActive())
	{
		HierarchyWidth += ImGui::GetIO().MouseDelta.x;
		HierarchyWidth = std::max(100.0f, std::min(HierarchyWidth, ImGui::GetWindowWidth() - DetailsWidth - 100.0f));
	}
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	ImGui::PopStyleColor(3);

	ImGui::SameLine();

	// Center: viewport
	ImGui::BeginGroup();
	{
		float  ViewportWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		ImVec2 Size = ImVec2(ViewportWidth, ImGui::GetContentRegionAvail().y);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// Right: bone details
	ImGui::BeginChild("BoneDetails", ImVec2(DetailsWidth, 0), true);
	ImGui::Text("Bone Details");
	ImGui::Separator();

	if (SkeletalMesh && SelectedBoneIndex != -1)
	{
		FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		FBone& Bone = Asset->Bones[SelectedBoneIndex];

		ImGui::Text("Name: %s", Bone.Name.c_str());
		ImGui::Text("Index: %d", SelectedBoneIndex);
		ImGui::Dummy(ImVec2(0, 10));

		USkeletalMeshComponent* PreviewMeshComponent = ViewportClient.GetPreviewMeshComponent();
		FTransform LocalTransform = PreviewMeshComponent
			? PreviewMeshComponent->GetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex)
			: FTransform(Bone.GetReferenceLocalPose());

		FVector Location = LocalTransform.Location;
		if (ImGui::DragFloat3("Location", &Location.X, 0.1f))
		{
			LocalTransform.Location = Location;
			if (PreviewMeshComponent)
				PreviewMeshComponent->SetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex, LocalTransform);
			else
			{
				Bone.ReferenceLocalPose = LocalTransform.ToMatrix();
				Bone.SyncLegacyPoseDataFromSeparated();
			}
		}

		FVector Rotation = LocalTransform.GetRotator().ToVector();
		if (ImGui::DragFloat3("Rotation", &Rotation.X, 0.1f))
		{
			LocalTransform.SetRotation(FRotator(Rotation));
			if (PreviewMeshComponent)
				PreviewMeshComponent->SetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex, LocalTransform);
			else
			{
				Bone.ReferenceLocalPose = LocalTransform.ToMatrix();
				Bone.SyncLegacyPoseDataFromSeparated();
			}
		}

		FVector Scale = LocalTransform.Scale;
		if (ImGui::DragFloat3("Scale", &Scale.X, 0.1f, 0.01f))
		{
			LocalTransform.Scale = Scale;
			if (PreviewMeshComponent)
				PreviewMeshComponent->SetBoneEditBaseLocalTransformByIndex(SelectedBoneIndex, LocalTransform);
			else
			{
				Bone.ReferenceLocalPose = LocalTransform.ToMatrix();
				Bone.SyncLegacyPoseDataFromSeparated();
			}
		}
	}
	else
	{
		ImGui::TextDisabled("Select a bone to edit.");
	}

	ImGui::EndChild();
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh tab
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderMeshLayout()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);

	// Left: mesh info
	const float StatsWidth = 220.0f;
	ImGui::BeginChild("MeshInfo", ImVec2(StatsWidth, 0), true);
	ImGui::Text("Mesh Info");
	ImGui::Separator();
	if (SkeletalMesh)
	{
		const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		if (Asset)
		{
			ImGui::Text("Vertices:  %s", FormatMeshStatCount(Asset->Vertices.size()).c_str());
			ImGui::Text("Triangles: %s", FormatMeshStatCount(Asset->Indices.size() / 3).c_str());
			ImGui::Text("Bones:     %zu", Asset->Bones.size());
			ImGui::Text("Morphs:    %zu", Asset->MorphTargets.size());
			USkeletalMeshComponent* PreviewMeshComponent = ViewportClient.GetPreviewMeshComponent();
			if (!Asset->MorphTargets.empty() && PreviewMeshComponent)
			{
				ImGui::Dummy(ImVec2(0, 8));
				ImGui::Separator();
				ImGui::TextUnformatted("Morph Preview");
				if (ImGui::SmallButton("Reset Morphs"))
				{
					PreviewMeshComponent->ClearMorphTargetWeights();
				}
				for (int32 MorphIndex = 0; MorphIndex < static_cast<int32>(Asset->MorphTargets.size()); ++MorphIndex)
				{
					const FMorphTarget& MorphTarget = Asset->MorphTargets[MorphIndex];
					float               Weight = PreviewMeshComponent->GetMorphTargetWeightByIndex(MorphIndex);
					ImGui::PushID(MorphIndex);
					const char* Label = MorphTarget.Name.empty() ? "Unnamed" : MorphTarget.Name.c_str();
					if (ImGui::SliderFloat(Label, &Weight, -1.0f, 1.0f, "%.3f"))
					{
						PreviewMeshComponent->SetMorphTargetWeightByIndex(MorphIndex, Weight);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%zu vertex deltas", MorphTarget.Deltas.size());
					}
					ImGui::PopID();
				}
			}
			ImGui::Dummy(ImVec2(0, 8));
			const FString& Path = SkeletalMesh->GetAssetPathFileName();
			if (!Path.empty() && Path != "None")
			{
				ImGui::TextWrapped("Path:\n%s", Path.c_str());
			}
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Center: viewport (full remaining width)
	ImGui::BeginGroup();
	{
		ImVec2 Size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();
}

// ─────────────────────────────────────────────────────────────────────────────
// Animation tab
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderAnimationLayout(float TotalHeight)
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);

	constexpr float TimelineHeight = 210.0f;
	const float     ContentHeight = TotalHeight - TimelineHeight - ImGui::GetStyle().ItemSpacing.y * 3.0f;

	// ─── Top: Asset Details | Viewport | Asset Browser (Persona 배치) ───

	// Left: 시퀀스 / 몽타주 디테일 패널 (선택 종류에 따라 분기)
	ImGui::BeginChild("AssetDetails", ImVec2(AnimTabState.AnimDetailsWidth, ContentHeight), true);
	if (AnimTabState.bMontageSelected && AnimTabState.CurrentMontage)
	{
		USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
		UAnimInstance* AnimInst = Comp ? Comp->GetAnimInstance() : nullptr;
		FAnimMontagePropertyPanel::Render(AnimTabState.CurrentMontage, Comp, AnimInst);
	}
	else if (AnimTabState.CurrentSequence)
	{
		UAnimSequence* Seq = AnimTabState.CurrentSequence;
		// Notify entry 가 타임라인에서 선택되어 있으면 Notify 의 UPROPERTY 편집 UI 를 표시.
		// 아니면 기존 시퀀스 메타 + Root Motion 패널.
		const int32 NotifyCount = static_cast<int32>(Seq->GetNotifies().size());
		const bool bShowNotifyDetails =
			AnimTabState.SelectedNotifyIndex >= 0 &&
			AnimTabState.SelectedNotifyIndex < NotifyCount;
		const bool bShowMorphDetails = AnimTabState.SelectedMorphCurveIndex >= 0 && AnimTabState.SelectedMorphCurveIndex
			< static_cast<int32>(Seq->GetMorphTargetCurves().size());

		if (bShowNotifyDetails)
		{
			FAnimationTimelinePanel::RenderNotifyDetails(Seq, AnimTabState.SelectedNotifyIndex);
		}
		else if (bShowMorphDetails)
		{
			if (FAnimationTimelinePanel::RenderMorphDetails(
				Seq,
				SkeletalMesh,
				AnimTabState.SelectedMorphCurveIndex,
				AnimTabState.SelectedMorphKeyIndex
			))
			{
				RefreshAnimationPreviewPose();
			}
		}
		else
		{
			ImGui::TextUnformatted("Asset Details");
			ImGui::Separator();
			ImGui::Text("Name:   %s", Seq->GetName().c_str());
			ImGui::Text("Length: %.3f s", Seq->GetPlayLength());
			ImGui::Text("FPS:    %.1f", Seq->GetFrameRate());
			ImGui::Text("Frames: %d", Seq->GetNumberOfFrames());
			ImGui::Dummy(ImVec2(0, 6));
			const FString& Path = Seq->GetAssetPathFileName();
			if (!Path.empty() && Path != "None")
			{
				ImGui::TextWrapped("Path:\n%s", Path.c_str());
			}

			// AnimSequence property 패널 — root motion 등 편집 가능한 항목.
			ImGui::Dummy(ImVec2(0, 12));
			FAnimSequencePropertyPanel::Render(Seq);

			USkeletalMeshComponent* PreviewMeshComponent = ViewportClient.GetPreviewMeshComponent();
			USkeletalMesh* PreviewMesh = PreviewMeshComponent ? PreviewMeshComponent->GetSkeletalMesh() : SkeletalMesh;
			FSkeletalMesh* MeshAsset = PreviewMesh ? PreviewMesh->GetSkeletalMeshAsset() : nullptr;
			if (MeshAsset && !MeshAsset->MorphTargets.empty())
			{
				ImGui::Dummy(ImVec2(0, 12));
				ImGui::Separator();
				ImGui::TextUnformatted("Morph Preview / Keys");
				EnsureMorphPreviewOverrideSize();
				if (ImGui::SmallButton("Clear Morph Preview"))
				{
					ResetMorphPreviewOverrides();
					RefreshAnimationPreviewPose();
				}
				for (int32 MorphIndex = 0; MorphIndex < static_cast<int32>(MeshAsset->MorphTargets.size()); ++
					MorphIndex)
				{
					const FMorphTarget& MorphTarget = MeshAsset->MorphTargets[MorphIndex];
					float               CurrentWeight = 0.0f;
					if (MorphIndex < static_cast<int32>(AnimTabState.MorphPreviewWeights.size()) && AnimTabState.
						MorphPreviewOverrideMask[MorphIndex] != 0)
					{
						CurrentWeight = AnimTabState.MorphPreviewWeights[MorphIndex];
					}
					else if (PreviewMeshComponent)
					{
						CurrentWeight = PreviewMeshComponent->GetMorphTargetWeightByIndex(MorphIndex);
					}

					ImGui::PushID(MorphIndex);
					const char* Label = MorphTarget.Name.empty() ? "Unnamed" : MorphTarget.Name.c_str();
					if (ImGui::SliderFloat(Label, &CurrentWeight, -1.0f, 1.0f, "%.3f"))
					{
						AnimTabState.MorphPreviewWeights[MorphIndex] = CurrentWeight;
						AnimTabState.MorphPreviewOverrideMask[MorphIndex] = 1;
						AnimTabState.bMorphPreviewOverrideEnabled = true;
						RefreshAnimationPreviewPose();
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Key"))
					{
						FMorphTargetCurve& Curve = FindOrAddMorphCurve(Seq, MorphTarget.Name);
						AddOrUpdateMorphCurveKey(
							Curve,
							PreviewMeshComponent && PreviewMeshComponent->GetAnimNodeInstance(FName::None)
							? PreviewMeshComponent->GetAnimNodeInstance(FName::None)->GetCurrentTime() : 0.0f,
							CurrentWeight
						);
						AnimTabState.MorphPreviewOverrideMask[MorphIndex] = 0;
						bool bAnyOverride = false;
						for (uint8 Mask : AnimTabState.MorphPreviewOverrideMask)
						{
							if (Mask != 0)
							{
								bAnyOverride = true;
								break;
							}
						}
						AnimTabState.bMorphPreviewOverrideEnabled = bAnyOverride;
						FAnimationManager::Get().SaveAnimationPreservingMetadata(Seq);
						RefreshAnimationPreviewPose();
					}
					ImGui::PopID();
				}
			}
		}
	}
	else
	{
		ImGui::TextUnformatted("Asset Details");
		ImGui::Separator();
		ImGui::TextDisabled("No animation selected.");
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Center: viewport
	ImGui::BeginGroup();
	{
		float  ViewportWidth = ImGui::GetContentRegionAvail().x - AnimTabState.AnimListWidth - ImGui::GetStyle().ItemSpacing.x;
		ImVec2 Size = ImVec2(ViewportWidth, ContentHeight);
		RenderViewportPanel(Size);
	}
	ImGui::EndGroup();

	ImGui::SameLine();

	// Right: 에셋 브라우저 (애니메이션 목록)
	ImGui::BeginChild("AssetBrowser", ImVec2(AnimTabState.AnimListWidth, ContentHeight), true);
	ImGui::TextUnformatted("Asset Browser");
	ImGui::Separator();

	if (ImGui::Button("Load...", ImVec2(-1.0f, 0.0f)))
	{
		FEditorFileDialogOptions Opts;
		Opts.Filter = L"Animation Files (*.uasset)\0*.uasset\0All Files (*.*)\0*.*\0";
		Opts.Title = L"Load Animation";
		Opts.bReturnRelativeToProjectRoot = true;
		FString Path = FEditorFileUtils::OpenFileDialog(Opts);
		if (!Path.empty())
		{
			UAnimSequence* Seq = FAnimationManager::Get().LoadAnimation(Path);
			if (Seq && Seq->IsCompatibleWith(SkeletalMesh))
			{
				AnimTabState.CurrentSequence = Seq;
				AnimTabState.SelectedAnimIndex = -1;
				AnimTabState.SelectedNotifyIndex = -1;
				AnimTabState.SelectedMorphCurveIndex = -1;
				AnimTabState.SelectedMorphKeyIndex = -1;
				ApplyAnimationToComponent();
			}
		}
	}

	if (ImGui::Button("Import Animation FBX", ImVec2(-1.0f, 0.0f)))
	{
		FEditorFileDialogOptions Opts;
		Opts.Filter = L"FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
		Opts.Title = L"Import Animation FBX";
		Opts.bReturnRelativeToProjectRoot = true;
		FString Path = FEditorFileUtils::OpenFileDialog(Opts);
		if (!Path.empty())
		{
			FFbxImportOptionsDialog::BeginAnimationImport(AnimTabState.AnimationImportDialog, Path);
		}
	}

	if (ImGui::Button("+ New Morph Animation", ImVec2(-1.0f, 0.0f)) && SkeletalMesh)
	{
		UAnimSequence* Seq = UObjectManager::Get().CreateObject<UAnimSequence>();
		UAnimDataModel* DataModel = UObjectManager::Get().CreateObject<UAnimDataModel>(Seq);
		DataModel->SetTiming(1.0f, 30.0f, 0);
		Seq->SetDataModel(DataModel);
		Seq->SetSkeletonBinding(SkeletalMesh->GetSkeletonBinding());
		Seq->SetFName(FName("MorphAnimation"));
		const FString AnimPath = FAnimationManager::GetAnimationPathForSkeleton(
			SkeletalMesh->GetAssetPathFileName(),
			"MorphAnimation",
			SkeletalMesh->GetSkeletonBinding().SkeletonPath
		);
		if (FAnimationManager::Get().SaveAnimation(Seq, AnimPath, SkeletalMesh->GetAssetPathFileName()))
		{
			AnimTabState.CurrentSequence = Seq;
			AnimTabState.SelectedAnimIndex = -1;
			AnimTabState.SelectedNotifyIndex = -1;
			AnimTabState.SelectedMorphCurveIndex = -1;
			AnimTabState.SelectedMorphKeyIndex = -1;
			ApplyAnimationToComponent();
			FAnimationManager::Get().RefreshAvailableAnimations();
			MarkAnimationListDirty();
		}
	}

	FAnimationImportRequest      AnimationImportRequest;
	const EFbxImportDialogResult AnimationImportDialogResult = FFbxImportOptionsDialog::RenderAnimationImportPopup(
		"Import Animation FBX Options",
		AnimTabState.AnimationImportDialog,
		SkeletalMesh ? SkeletalMesh->GetSkeletonBinding().SkeletonPath : FString("None"),
		AnimationImportRequest
	);

	if (AnimationImportDialogResult == EFbxImportDialogResult::Submitted)
	{
		TArray<UAnimSequence*> ImportedSequences;
		FAnimationManager::Get().ImportAnimationForSkeleton(AnimationImportRequest, &ImportedSequences);
		// 임포트 성공/스킵(이미 존재) 무관하게 디스크를 다시 스캔해 목록 갱신.
		FAnimationManager::Get().RefreshAvailableAnimations();
		MarkAnimationListDirty();
		if (!ImportedSequences.empty())
		{
			AnimTabState.CurrentSequence = ImportedSequences[0];
			AnimTabState.SelectedAnimIndex = -1;
			AnimTabState.SelectedNotifyIndex = -1;
			AnimTabState.SelectedMorphCurveIndex = -1;
			AnimTabState.SelectedMorphKeyIndex = -1;
			ApplyAnimationToComponent();
			FFbxImportOptionsDialog::RequestClose(AnimTabState.AnimationImportDialog);
		}
		else
		{
			AnimTabState.AnimationImportDialog.Error =
				"No animation was imported. Existing assets may have been skipped.";
		}
	}

	ImGui::Separator();

	if (ImGui::SmallButton("Refresh Animation List"))
	{
		FAnimationManager::Get().RefreshAvailableAnimations();
		FAnimationManager::Get().RefreshAvailableMontages();
		MarkAnimationListDirty();
	}

	// 디스크 스캔 — montage 목록 초기화 (최초 1회 + Refresh 시).
	static bool sMontagesScanned = false;
	if (!sMontagesScanned)
	{
		FAnimationManager::Get().RefreshAvailableMontages();
		sMontagesScanned = true;
	}

	const TArray<FAssetListItem>& AnimFiles = GetCachedAnimationFilesForCurrentSkeleton();
	const TArray<FAssetListItem>& MontageFiles = FAnimationManager::Get().GetAvailableMontageFiles();

	// asset 경로의 stem (확장자/디렉토리 제거) — 자동 montage 이름의 source 식별자.
	auto ExtractStem = [](const FString& Path) -> FString
		{
			const size_t LastSlash = Path.find_last_of("/\\");
			const size_t Start = (LastSlash == FString::npos) ? 0 : LastSlash + 1;
			const size_t LastDot = Path.find_last_of('.');
			const size_t End = (LastDot == FString::npos || LastDot < Start) ? Path.size() : LastDot;
			return Path.substr(Start, End - Start);
		};

	// + New Montage — 현재 선택된 sequence 가 있으면 source 로 새 montage 생성.
	// 이름은 sequence 의 asset path stem 사용 (UObject::GetName() 의 자동생성 ObjectName 회피).
	const bool bCanCreateMontage = (AnimTabState.CurrentSequence != nullptr) && !AnimTabState.bMontageSelected;
	if (!bCanCreateMontage) ImGui::BeginDisabled();
	if (ImGui::Button("+ New Montage (from selected sequence)", ImVec2(-1.0f, 0.0f)))
	{
		const FString Stem = ExtractStem(AnimTabState.CurrentSequence->GetAssetPathFileName());
		const FString MontageName = Stem + "_Montage";
		const FString PackagePath = FString("Content/Montages/") + MontageName + ".uasset";
		UAnimMontage* Montage = FAnimationManager::Get().CreateMontage(AnimTabState.CurrentSequence, MontageName);
		if (Montage)
		{
			FAnimationManager::Get().SaveMontage(Montage, PackagePath);
			FAnimationManager::Get().RefreshAvailableMontages();
			AnimTabState.CurrentMontage = Montage;
			AnimTabState.bMontageSelected = true;

			// 새 montage 의 인덱스 즉시 매핑 — list 의 hilight + 다음 클릭의 일관 동작 보장.
			const TArray<FAssetListItem>& Updated = FAnimationManager::Get().GetAvailableMontageFiles();
			AnimTabState.SelectedMontageIndex = -1;
			for (int32 j = 0; j < static_cast<int32>(Updated.size()); ++j)
			{
				if (Updated[j].FullPath == PackagePath)
				{
					AnimTabState.SelectedMontageIndex = j;
					break;
				}
			}
		}
	}
	if (!bCanCreateMontage) ImGui::EndDisabled();

	// 통합 리스트 — Sequence + Montage 한 selectable. 알파벳 정렬 (Walking_mixamo_com 옆에
	// Walking_mixamo_com_Montage 가 자연스럽게 인접). 시각 구분: Montage 는 노랑 + [M] prefix.
	struct FEntry
	{
		FString  DisplayName;
		FString  FullPath;
		bool     bIsMontage = false;
		int32    OriginalIndex = -1;   // AnimFiles 또는 MontageFiles 의 인덱스
	};
	TArray<FEntry> Entries;
	Entries.reserve(AnimFiles.size() + MontageFiles.size());
	for (int32 i = 0; i < static_cast<int32>(AnimFiles.size()); ++i) Entries.push_back({ AnimFiles[i].DisplayName,    AnimFiles[i].FullPath,    false, i });
	for (int32 i = 0; i < static_cast<int32>(MontageFiles.size()); ++i) Entries.push_back({ MontageFiles[i].DisplayName, MontageFiles[i].FullPath, true,  i });
	std::sort(Entries.begin(), Entries.end(),
		[](const FEntry& A, const FEntry& B) { return A.DisplayName < B.DisplayName; });

	ImGui::TextUnformatted("Animations & Montages");
	for (const FEntry& E : Entries)
	{
		const bool bSelected =
			E.bIsMontage
			? (AnimTabState.bMontageSelected && AnimTabState.SelectedMontageIndex == E.OriginalIndex)
			: (!AnimTabState.bMontageSelected && AnimTabState.SelectedAnimIndex == E.OriginalIndex);

		// 시각 구분 — Montage 는 노랑 톤. Sequence 는 기본 색.
		const ImU32 Color = E.bIsMontage ? IM_COL32(255, 200, 100, 255) : IM_COL32(255, 255, 255, 255);
		ImGui::PushStyleColor(ImGuiCol_Text, Color);

		const FString Label = (E.bIsMontage ? "[M] " : "      ") + E.DisplayName;
		if (ImGui::Selectable(Label.c_str(), bSelected))
		{
			if (E.bIsMontage)
			{
				AnimTabState.SelectedMontageIndex = E.OriginalIndex;
				AnimTabState.bMontageSelected = true;
				AnimTabState.SelectedNotifyIndex = -1;
				AnimTabState.SelectedMorphCurveIndex = -1;
				AnimTabState.SelectedMorphKeyIndex = -1;
				ResetMorphPreviewOverrides();
				if (UAnimMontage* M = FAnimationManager::Get().LoadMontage(E.FullPath))
				{
					AnimTabState.CurrentMontage = M;
				}
			}
			else
			{
				AnimTabState.SelectedAnimIndex = E.OriginalIndex;
				AnimTabState.bMontageSelected = false;
				AnimTabState.SelectedNotifyIndex = -1;
				AnimTabState.SelectedMorphCurveIndex = -1;
				AnimTabState.SelectedMorphKeyIndex = -1;
				if (UAnimSequence* Seq = FAnimationManager::Get().LoadAnimation(E.FullPath))
				{
					if (Seq->IsCompatibleWith(SkeletalMesh))
					{
						AnimTabState.CurrentSequence = Seq;
						ApplyAnimationToComponent();
					}
				}
			}
		}
		ImGui::PopStyleColor();

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s\n%s", E.bIsMontage ? "Montage" : "Sequence", E.FullPath.c_str());
		}
	}
	ImGui::EndChild();

	// ─── Bottom: Unreal 시퀀서 패널 ───
	UAnimSingleNodeInstance* NodeInst = nullptr;
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	if (Comp && AnimTabState.CurrentSequence)
	{
		NodeInst = Comp->GetAnimNodeInstance(FName::None);
	}

	// 스페이스바: 재생/정지 토글 (메시 에디터 창 포커스 + 텍스트 입력 중 아닐 때)
	if (Comp && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::GetIO().WantTextInput &&
		ImGui::IsKeyPressed(ImGuiKey_Space, false))
	{
		const bool bPlaying = NodeInst && NodeInst->IsPlaying();
		Comp->SetPlaying(!bPlaying);
	}

	FAnimationTimelinePanel::Render(NodeInst, Comp, AnimTabState.CurrentSequence, TimelineHeight,
		AnimTabState.SelectedNotifyIndex,
		AnimTabState.SelectedMorphCurveIndex,
		AnimTabState.SelectedMorphKeyIndex
	);
}

// ─────────────────────────────────────────────────────────────────────────────
// Physical Asset tab
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderMeshStatsOverlay(ImDrawList* DrawList, const ImVec2& ViewportPos) const
{
	if (!DrawList || !EditedObject)
	{
		return;
	}

	size_t VertexCount = 0;
	size_t TriangleCount = 0;
	size_t IndexCount = 0;
	double ImportSeconds = -1.0;

	if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject))
	{
		if (const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset())
		{
			VertexCount = Asset->Vertices.size();
			IndexCount = Asset->Indices.size();
			TriangleCount = Asset->Indices.size() / 3;
		}
		ImportSeconds = GetRecordedImportDurationSeconds(SkeletalMesh);
	}

	FString Text =
		"Triangles: " + FormatMeshStatCount(TriangleCount) + "\n" +
		"Vertices: " + FormatMeshStatCount(VertexCount) + "\n" +
		"Indices: " + FormatMeshStatCount(IndexCount);

	if (ImportSeconds >= 0.0)
	{
		Text += "\nImport Time: " + FormatMeshStatSeconds(ImportSeconds);
	}

	const ImVec2 TextPos(ViewportPos.x + 8.0f, ViewportPos.y + 36.0f);
	DrawList->AddText(ImVec2(TextPos.x + 1.0f, TextPos.y + 1.0f), IM_COL32(0, 0, 0, 220), Text.c_str());
	DrawList->AddText(TextPos, IM_COL32(235, 238, 242, 255), Text.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Bone tree (Skeleton tab)
// ─────────────────────────────────────────────────────────────────────────────

void FMeshEditorWidget::RenderBoneTree(const FSkeletalMesh* Asset, int32 Index)
{
	const FBone& Bone = Asset->Bones[Index];

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;

	if (Index == SelectedBoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	//자식이 존재하는 지 검사
	bool bHasChildren = false;
	for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
	{
		if (Asset->Bones[i].ParentIndex == Index)
		{
			bHasChildren = true;
			break;
		}
	}

	if (!bHasChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	bool bOpen = ImGui::TreeNodeEx(Bone.Name.c_str(), Flags);

	if (ImGui::IsItemClicked())
	{
		SelectedBoneIndex = Index;
		SelectedBodySetup = nullptr;
		PhysicsGraphFocusBodySetup = nullptr;
		SelectedConstraintIndex = -1;
		ViewportClient.SetSelectedBone(Cast<USkeletalMesh>(EditedObject), Index);
	}

	if (bOpen && bHasChildren)
	{
		for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
		{
			if (Asset->Bones[i].ParentIndex == Index)
			{
				RenderBoneTree(Asset, i);
			}
		}
		ImGui::TreePop();
	}
}
