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

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

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

	FKShapeElem* GetFirstPhysicsShapeElem(UBodySetup* BodySetup, const char** OutShapeType = nullptr)
	{
		if (OutShapeType)
		{
			*OutShapeType = "None";
		}

		if (!BodySetup)
		{
			return nullptr;
		}

		FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
		if (!AggGeom.SphereElems.empty())
		{
			if (OutShapeType) *OutShapeType = "Sphere";
			return &AggGeom.SphereElems[0];
		}
		if (!AggGeom.BoxElems.empty())
		{
			if (OutShapeType) *OutShapeType = "Box";
			return &AggGeom.BoxElems[0];
		}
		if (!AggGeom.SphylElems.empty())
		{
			if (OutShapeType) *OutShapeType = "Capsule";
			return &AggGeom.SphylElems[0];
		}
		if (!AggGeom.ConvexElems.empty())
		{
			if (OutShapeType) *OutShapeType = "Convex";
			return &AggGeom.ConvexElems[0];
		}
		return nullptr;
	}

	struct FPhysicsConstraintCandidate
	{
		UBodySetup* BodySetup = nullptr;
		FName ParentBoneName = FName::None;
		FName ChildBoneName = FName::None;
	};

	enum class EManualPhysicsBodyShape : uint8
	{
		Sphere,
		Capsule,
		Box
	};

	FString MakePhysicsGraphNodeKey(const UBodySetup* BodySetup)
	{
		if (!BodySetup)
		{
			return "None";
		}
		return BodySetup->GetBoneName().ToString() + "#" + std::to_string(BodySetup->GetUUID());
	}

	FString MakePhysicsGraphConstraintNodeKey(int32 ConstraintIndex)
	{
		return "Constraint#" + std::to_string(ConstraintIndex);
	}

	int32 FindBoneIndexByName(const FSkeletalMesh* Asset, const FName& BoneName)
	{
		if (!Asset)
		{
			return -1;
		}

		const FString BoneNameString = BoneName.ToString();
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (Asset->Bones[BoneIndex].Name == BoneNameString)
			{
				return BoneIndex;
			}
		}
		return -1;
	}

	float GetDefaultManualBodySize(const FSkeletalMesh* Asset, int32 BoneIndex)
	{
		if (!Asset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size()))
		{
			return 0.1f;
		}

		const FVector BoneLocation = Asset->Bones[BoneIndex].GetReferenceGlobalPose().GetLocation();
		float BestChildDistance = 0.0f;
		for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Asset->Bones.size()); ++ChildIndex)
		{
			if (Asset->Bones[ChildIndex].ParentIndex != BoneIndex)
			{
				continue;
			}

			const float Distance = FVector::Distance(BoneLocation, Asset->Bones[ChildIndex].GetReferenceGlobalPose().GetLocation());
			if (Distance > BestChildDistance)
			{
				BestChildDistance = Distance;
			}
		}

		return std::max(0.04f, BestChildDistance > 0.0f ? BestChildDistance * 0.25f : 0.1f);
	}

	UBodySetup* AddManualPhysicsBodyForBone(USkeletalMesh* SkeletalMesh, int32 BoneIndex, EManualPhysicsBodyShape Shape)
	{
		FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
		UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->EnsurePhysicsAsset() : nullptr;
		if (!SkeletalMesh || !Asset || !PhysAsset || BoneIndex < 0 || BoneIndex >= static_cast<int32>(Asset->Bones.size()))
		{
			return nullptr;
		}

		const FBone& Bone = Asset->Bones[BoneIndex];
		const FName BoneName(Bone.Name);
		if (PhysAsset->FindBodySetupByBoneName(BoneName))
		{
			return nullptr;
		}

		UBodySetup* BodySetup = UObjectManager::Get().CreateObject<UBodySetup>(PhysAsset);
		if (!BodySetup)
		{
			return nullptr;
		}

		const float Size = GetDefaultManualBodySize(Asset, BoneIndex);
		BodySetup->SetBoneName(BoneName);
		BodySetup->SetFName(FName(Bone.Name + "_BodySetup"));

		FKAggregateGeom& AggGeom = BodySetup->GetAggGeom();
		if (Shape == EManualPhysicsBodyShape::Sphere)
		{
			FKSphereElem Sphere;
			Sphere.Name = Bone.Name + "_SphereBody";
			Sphere.Radius = Size;
			Sphere.Transform.Location = FVector::ZeroVector;
			Sphere.Transform.Rotation = FQuat::Identity;
			Sphere.Transform.Scale = FVector::OneVector;
			AggGeom.SphereElems.push_back(Sphere);
		}
		else if (Shape == EManualPhysicsBodyShape::Capsule)
		{
			FKSphylElem Capsule;
			Capsule.Name = Bone.Name + "_CapsuleBody";
			Capsule.Radius = Size * 0.5f;
			Capsule.Length = Size * 2.0f;
			Capsule.Transform.Location = FVector::ZeroVector;
			Capsule.Transform.Rotation = FQuat::Identity;
			Capsule.Transform.Scale = FVector::OneVector;
			AggGeom.SphylElems.push_back(Capsule);
		}
		else
		{
			FKBoxElem Box;
			Box.Name = Bone.Name + "_BoxBody";
			Box.Extent = FVector(Size, Size, Size);
			Box.Transform.Location = FVector::ZeroVector;
			Box.Transform.Rotation = FQuat::Identity;
			Box.Transform.Scale = FVector::OneVector;
			AggGeom.BoxElems.push_back(Box);
		}

		PhysAsset->BodySetups.push_back(BodySetup);
		return BodySetup;
	}

	bool RemovePhysicsBodyAndConstraints(UPhysicsAsset* PhysAsset, UBodySetup* BodySetup)
	{
		if (!PhysAsset || !BodySetup)
		{
			return false;
		}

		const FName BoneName = BodySetup->GetBoneName();
		bool bRemoved = false;
		for (int32 ConstraintIndex = static_cast<int32>(PhysAsset->ConstraintSetups.size()) - 1; ConstraintIndex >= 0; --ConstraintIndex)
		{
			const FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[ConstraintIndex];
			if (Constraint.ParentBoneName == BoneName || Constraint.ChildBoneName == BoneName)
			{
				PhysAsset->ConstraintSetups.erase(PhysAsset->ConstraintSetups.begin() + ConstraintIndex);
				bRemoved = true;
			}
		}

		for (int32 BodyIndex = static_cast<int32>(PhysAsset->BodySetups.size()) - 1; BodyIndex >= 0; --BodyIndex)
		{
			if (PhysAsset->BodySetups[BodyIndex] == BodySetup)
			{
				PhysAsset->BodySetups.erase(PhysAsset->BodySetups.begin() + BodyIndex);
				UObjectManager::Get().DestroyObject(BodySetup);
				bRemoved = true;
				break;
			}
		}
		return bRemoved;
	}

	TArray<FPhysicsConstraintCandidate> BuildStrictConstraintCandidates(
		USkeletalMesh* SkeletalMesh,
		UPhysicsAsset* PhysAsset,
		UBodySetup* SourceBody)
	{
		TArray<FPhysicsConstraintCandidate> Candidates;
		FSkeletalMesh* Asset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
		if (!SkeletalMesh || !PhysAsset || !SourceBody || !Asset)
		{
			return Candidates;
		}

		const int32 SourceBoneIndex = FindBoneIndexByName(Asset, SourceBody->GetBoneName());
		if (SourceBoneIndex < 0 || SourceBoneIndex >= static_cast<int32>(Asset->Bones.size()))
		{
			return Candidates;
		}

		auto TryAddCandidate = [&](int32 CandidateBoneIndex)
		{
			if (CandidateBoneIndex < 0 || CandidateBoneIndex >= static_cast<int32>(Asset->Bones.size()))
			{
				return;
			}

			const FName CandidateBoneName(Asset->Bones[CandidateBoneIndex].Name);
			UBodySetup* CandidateBody = PhysAsset->FindBodySetupByBoneName(CandidateBoneName);
			if (!CandidateBody || SkeletalMesh->HasPhysicsConstraintBetweenBodies(SourceBody->GetBoneName(), CandidateBoneName))
			{
				return;
			}

			FPhysicsConstraintCandidate Candidate;
			Candidate.BodySetup = CandidateBody;
			if (Asset->Bones[SourceBoneIndex].ParentIndex == CandidateBoneIndex)
			{
				Candidate.ParentBoneName = CandidateBoneName;
				Candidate.ChildBoneName = SourceBody->GetBoneName();
			}
			else if (Asset->Bones[CandidateBoneIndex].ParentIndex == SourceBoneIndex)
			{
				Candidate.ParentBoneName = SourceBody->GetBoneName();
				Candidate.ChildBoneName = CandidateBoneName;
			}
			else
			{
				return;
			}
			Candidates.push_back(Candidate);
		};

		TryAddCandidate(Asset->Bones[SourceBoneIndex].ParentIndex);
		for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(Asset->Bones.size()); ++BoneIndex)
		{
			if (Asset->Bones[BoneIndex].ParentIndex == SourceBoneIndex)
			{
				TryAddCandidate(BoneIndex);
			}
		}
		return Candidates;
	}

	void CollectEditablePropertiesFromStruct(UStruct* Struct, void* Container, UObject* OwnerObject, TArray<FPropertyValue>& OutProps)
	{
		if (!Struct || !Container)
		{
			return;
		}

		TArray<const FProperty*> Properties;
		Struct->GetPropertyRefs(Properties);
		for (const FProperty* Property : Properties)
		{
			if (!Property || (Property->Flags & PF_Edit) == 0 || !Property->GetValuePtrFor(Container))
			{
				continue;
			}

			FPropertyValue Value = Property->ToValue(Container, OwnerObject);
			if (Value.PassesEditCondition())
			{
				OutProps.push_back(Value);
			}
		}
	}

	bool RenderReflectedPropertyTable(const char* TableId, TArray<FPropertyValue>& Props, bool bDispatchChange)
	{
		bool bChanged = false;
		if (Props.empty())
		{
			ImGui::TextDisabled("No editable reflected properties.");
			return false;
		}

		if (ImGui::BeginTable(TableId, 2,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 118.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

			ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.145f, 0.145f, 0.145f, 1.0f));

			FEditorPropertyRenderer PropertyRenderer;
			for (int32 i = 0; i < static_cast<int32>(Props.size()); ++i)
			{
				ImGui::TableNextRow();
				ImGui::PushID(i);

				ImGui::TableSetColumnIndex(0);
				const bool bPropertyOpen = FEditorPropertyRenderer::DrawPropertyLabel(Props[i]);

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1);

				FEditorPropertyRenderOptions Options;
				Options.bDispatchChange = bDispatchChange;
				Options.bUseExternalExpansion = true;
				Options.bParentExpanded = bPropertyOpen;
				if (PropertyRenderer.RenderPropertyWidget(Props, i, Options))
				{
					bChanged = true;
				}
				ImGui::PopID();
			}

			ImGui::PopStyleColor(2);
			ImGui::EndTable();
		}

		return bChanged;
	}

	void RegisterPreviewWorldCollisionComponents(UWorld* World, USkeletalMeshComponent* PreviewMeshComponent)
	{
		IPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
		if (!World || !PhysicsScene)
		{
			return;
		}

		for (AActor* Actor : World->GetActors())
		{
			if (!Actor)
			{
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents())
			{
				UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
				if (!Primitive || Primitive == PreviewMeshComponent || !Primitive->IsQueryCollisionEnabled())
				{
					continue;
				}
				PhysicsScene->RegisterComponent(Primitive);
			}
		}
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

	bool IsSameSkeletonBindingForAnimationList(const FSkeletonBinding& A, const FSkeletonBinding& B)
	{
		return A.SkeletonPath == B.SkeletonPath
			&& A.SkeletonAssetGuid == B.SkeletonAssetGuid
			&& A.CompatibilitySignature == B.CompatibilitySignature;
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
	return Object && Object->IsA<USkeletalMesh>();
}

bool FMeshEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const USkeletalMesh* CurrentMesh = Cast<USkeletalMesh>(EditedObject);
	const USkeletalMesh* RequestedMesh = Cast<USkeletalMesh>(Object);
	if (!IsOpen() || !CurrentMesh || !RequestedMesh)
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
	FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -0.05f));
	FloorActor->SetActorScale(FVector(10.0f, 10.0f, 0.02f));
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

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);

	ViewportClient.SetSelectedBone(Cast<USkeletalMesh>(EditedObject), -1);

	FSlateApplication::Get().RegisterViewport(&ViewportClient);

	// 디스크의 기존 AnimSequence .uasset 들을 목록에 채워 둔다(런타임 Load/Save 만으론 안 잡힘).
	FAnimationManager::Get().RefreshAvailableAnimations();

	ActiveTab = EMeshEditorTab::Skeleton;
	AnimTabState = FAnimationTabState{};
	SelectedBoneIndex = -1;
	SelectedBodySetup = nullptr;
	PhysicsGraphFocusBodySetup = nullptr;
	SelectedConstraintIndex = -1;
	PhysicsGraphNodePositions.clear();
}

void FMeshEditorWidget::Close()
{
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
	SelectedBodySetup = nullptr;
	PhysicsGraphFocusBodySetup = nullptr;
	SelectedConstraintIndex = -1;
	PhysicsGraphNodePositions.clear();
	bPhysicsAssetSimulationRunning = false;
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
				PhysicsScene->Tick(DeltaTime);
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
void FMeshEditorWidget::StartPhysicsAssetSimulation()
{
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	USkeletalMesh* Mesh = Comp ? Comp->GetSkeletalMesh() : nullptr;
	UPhysicsAsset* PhysAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	if (!Comp || !PhysAsset || !PhysAsset->HasAnyBodySetup())
	{
		return;
	}

	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		if (IPhysicsScene* PhysicsScene = PreviewWorld->EnsurePhysicsScene())
		{
			RegisterPreviewWorldCollisionComponents(PreviewWorld, Comp);
			if (Comp->IsRagdollSimulating())
			{
				Comp->SetSimulateRagdoll(false);
			}
			PhysicsScene->DestroyPhysicsAssetBodies(Comp);
			PhysicsScene->InstantiatePhysicsAssetBodies(Comp);
			PhysicsScene->SyncPhysicsAssetBodiesToComponentPose(Comp, true);
		}
	}

	Comp->SetSimulateRagdoll(true);
	bPhysicsAssetSimulationRunning = Comp->IsRagdollSimulating();
}

//Ragdoll 시뮬레이션을 멈추고, 필요하면 포즈를 리셋한다.
void FMeshEditorWidget::StopPhysicsAssetSimulation(bool bResetPose)
{
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	if (Comp)
	{
		Comp->SetSimulateRagdoll(false);
		if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
		{
			if (IPhysicsScene* PhysicsScene = PreviewWorld->GetPhysicsScene())
			{
				PhysicsScene->DestroyPhysicsAssetBodies(Comp);
			}
		}

		if (bResetPose)
		{
			Comp->ApplyBoneEditBasePose();
		}
	}

	bPhysicsAssetSimulationRunning = false;
}

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
			Close();
		}
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);
	}

	RenderTabBar();
	ImGui::Separator();
	ViewportClient.SetPhysicsAssetDebugDrawEnabled(ActiveTab == EMeshEditorTab::PhysicalAsset);
	ViewportClient.SetPhysicsAssetSolidDebugDrawEnabled(
		ActiveTab == EMeshEditorTab::PhysicalAsset && ViewportClient.GetRenderOptions().bShowPhysicsAssetSolid);
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

	ImGui::End();

	if (!bWindowOpen)
	{
		bPendingClose = true;
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
				ImGui::Checkbox("Physics Body Solid", &RenderOptions.bShowPhysicsAssetSolid);
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

void FMeshEditorWidget::ApplyAnimationToComponent()
{
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	if (!Comp || !AnimTabState.CurrentSequence)
	{
		return;
	}
	Comp->PlayAnimation(AnimTabState.CurrentSequence, /*bLooping=*/true);
	Comp->SetPlaying(false);
	Comp->SetPlayRate(1.0f);
	ResetMorphPreviewOverrides();
}

void FMeshEditorWidget::EnsureMorphPreviewOverrideSize()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	FSkeletalMesh* MeshAsset = SkeletalMesh ? SkeletalMesh->GetSkeletalMeshAsset() : nullptr;
	const size_t   MorphCount = MeshAsset ? MeshAsset->MorphTargets.size() : 0;
	if (AnimTabState.MorphPreviewWeights.size() != MorphCount)
	{
		AnimTabState.MorphPreviewWeights.assign(MorphCount, 0.0f);
	}
	if (AnimTabState.MorphPreviewOverrideMask.size() != MorphCount)
	{
		AnimTabState.MorphPreviewOverrideMask.assign(MorphCount, 0);
	}
}

void FMeshEditorWidget::ResetMorphPreviewOverrides()
{
	AnimTabState.MorphPreviewWeights.clear();
	AnimTabState.MorphPreviewOverrideMask.clear();
	AnimTabState.bMorphPreviewOverrideEnabled = false;
}

void FMeshEditorWidget::ApplyMorphPreviewOverrides(TArray<float>& InOutMorphWeights) const
{
	if (!AnimTabState.bMorphPreviewOverrideEnabled)
	{
		return;
	}
	const size_t Count = AnimTabState.MorphPreviewWeights.size();
	if (Count == 0 || AnimTabState.MorphPreviewOverrideMask.size() != Count)
	{
		return;
	}
	if (InOutMorphWeights.size() < Count)
	{
		InOutMorphWeights.resize(Count, 0.0f);
	}
	for (size_t Index = 0; Index < Count; ++Index)
	{
		if (AnimTabState.MorphPreviewOverrideMask[Index] != 0)
		{
			InOutMorphWeights[Index] = AnimTabState.MorphPreviewWeights[Index];
		}
	}
}

void FMeshEditorWidget::RefreshAnimationPreviewPose()
{
	USkeletalMeshComponent* Comp = ViewportClient.GetPreviewMeshComponent();
	if (!Comp) return;
	UAnimSingleNodeInstance* NodeInst = Comp->GetAnimNodeInstance(FName::None);
	if (!NodeInst) return;
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

void FMeshEditorWidget::MarkAnimationListDirty()
{
	AnimTabState.bAnimationListDirty = true;
}

const TArray<FAssetListItem>& FMeshEditorWidget::GetCachedAnimationFilesForCurrentSkeleton()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	FSkeletonBinding CurrentBinding;

	if (SkeletalMesh)
	{
		CurrentBinding = SkeletalMesh->GetSkeletonBinding();
	}
	else
	{
		CurrentBinding.Reset();
	}

	if (AnimTabState.bAnimationListDirty ||
		!IsSameSkeletonBindingForAnimationList(AnimTabState.CachedAnimationListBinding, CurrentBinding))
	{
		AnimTabState.CachedAnimationFiles.clear();
		AnimTabState.CachedAnimationListBinding = CurrentBinding;

		if (SkeletalMesh)
		{
			AnimTabState.CachedAnimationFiles = FAssetRegistry::ListAnimationsForSkeleton(CurrentBinding, false);
		}

		AnimTabState.bAnimationListDirty = false;
	}

	return AnimTabState.CachedAnimationFiles;
}

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

void FMeshEditorWidget::RenderPhysicsSimulationControls(USkeletalMesh* SkeletalMesh, UPhysicsAsset* PhysAsset)
{
	const bool bHasSimulatableAsset = SkeletalMesh && PhysAsset && PhysAsset->HasAnyBodySetup();
	constexpr float IconSize = 16.0f;
	constexpr float ButtonSize = 24.0f;

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.3f));

	auto DrawIconButton = [&](const char* Id, const wchar_t* IconFile, const char* FallbackLabel, bool bDisabled) -> bool
		{
			if (bDisabled)
			{
				ImGui::BeginDisabled();
			}

			bool bClicked = false;
			if (ID3D11ShaderResourceView* Icon = LoadEditorIcon(IconFile))
			{
				const ImVec2 ButtonPos = ImGui::GetCursorScreenPos();
				bClicked = ImGui::InvisibleButton(Id, ImVec2(ButtonSize, ButtonSize));
				const float IconOffset = (ButtonSize - IconSize) * 0.5f;
				ImGui::GetWindowDrawList()->AddImage(
					reinterpret_cast<ImTextureID>(Icon),
					ImVec2(ButtonPos.x + IconOffset, ButtonPos.y + IconOffset),
					ImVec2(ButtonPos.x + IconOffset + IconSize, ButtonPos.y + IconOffset + IconSize));
			}
			else
			{
				bClicked = ImGui::Button(FallbackLabel, ImVec2(ButtonSize, ButtonSize));
			}

			if (bDisabled)
			{
				ImGui::EndDisabled();
				bClicked = false;
			}
			return bClicked;
		};

	if (DrawIconButton("##PhysicsAssetSimPlay", L"icon_playInSelectedViewport_16x.png", ">", !bHasSimulatableAsset || bPhysicsAssetSimulationRunning))
	{
		StartPhysicsAssetSimulation();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Start PhysX simulation");
	}

	ImGui::SameLine(0.0f, 4.0f);

	if (DrawIconButton("##PhysicsAssetSimStop", L"generic_stop_16x.png", "[]", !bPhysicsAssetSimulationRunning))
	{
		StopPhysicsAssetSimulation(true);
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Stop PhysX simulation");
	}

	ImGui::SameLine();
	ImGui::TextDisabled("%s", bPhysicsAssetSimulationRunning ? "Simulating" : "Stopped");

	ImGui::PopStyleColor(3);
}

//PhysicsAsset 탭의 본 계층 구조 + 시뮬레이션 컨트롤 + 기타 편집 UI 렌더링. 선택된 본/바디/제약 조건에 따라 세부 패널도 함께 렌더링.
void FMeshEditorWidget::RenderPhysicalAssetLayout()
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
	UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
	if (PhysAsset && SelectedConstraintIndex >= static_cast<int32>(PhysAsset->ConstraintSetups.size()))
	{
		SelectedConstraintIndex = -1;
	}
	auto BodyBelongsToPhysicsAsset = [](UPhysicsAsset* InPhysAsset, UBodySetup* BodySetup) -> bool
	{
		if (!InPhysAsset || !BodySetup)
		{
			return false;
		}

		for (UBodySetup* Candidate : InPhysAsset->BodySetups)
		{
			if (Candidate == BodySetup)
			{
				return true;
			}
		}
		return false;
	};
	if (!PhysAsset)
	{
		SelectedBodySetup = nullptr;
		PhysicsGraphFocusBodySetup = nullptr;
		SelectedConstraintIndex = -1;
	}
	else
	{
		if (SelectedBodySetup && !BodyBelongsToPhysicsAsset(PhysAsset, SelectedBodySetup))
		{
			SelectedBodySetup = nullptr;
		}
		if (PhysicsGraphFocusBodySetup && !BodyBelongsToPhysicsAsset(PhysAsset, PhysicsGraphFocusBodySetup))
		{
			PhysicsGraphFocusBodySetup = nullptr;
		}
	}

	// Left: bone hierarchy
	const float LeftPanelHeight = ImGui::GetContentRegionAvail().y;
	const float GraphHeight = std::min(260.0f, std::max(160.0f, LeftPanelHeight * 0.32f));
	ImGui::BeginGroup();
	ImGui::BeginChild("BoneHierarchy", ImVec2(HierarchyWidth, std::max(120.0f, LeftPanelHeight - GraphHeight - ImGui::GetStyle().ItemSpacing.y)), true);
	ImGui::Text("Physics Asset");
	ImGui::Separator();

	//Auto generate bodies 버튼 + 시뮬레이션 컨트롤
	if (SkeletalMesh)
	{
		ImGui::Text("Bodies: %d", PhysAsset ? static_cast<int32>(PhysAsset->BodySetups.size()) : 0);
		ImGui::Text("Constraints: %d", PhysAsset ? static_cast<int32>(PhysAsset->ConstraintSetups.size()) : 0);
		RenderPhysicsSimulationControls(SkeletalMesh, PhysAsset);

		// Auto generate bodies 버튼
		if (ImGui::Button("Generate Bodies", ImVec2(-1.0f, 0.0f)))
		{
			StopPhysicsAssetSimulation(true);
			PhysAsset = SkeletalMesh->EnsurePhysicsAsset();

			FPhysicsAssetAutoGenerateSettings Settings;
			Settings.bReplaceExisting = true;
			Settings.bCreateConstraints = true;
			Settings.bUseDominantBoneOnly = true;
			Settings.bUseDefaultNameFilters = true;
			Settings.MinBoneWeight = 0.25f;
			Settings.LowerPercentile = 0.05f;
			Settings.UpperPercentile = 0.95f;
			Settings.ShapePadding = 1.10f;
			Settings.MinShapeSize = 0.01f;
			Settings.MinVertexCount = 12;

			if (PhysAsset->AutoGeneratePrimitiveBodiesFromSkeletalMesh(*(SkeletalMesh->GetSkeletalMeshAsset()), Settings))
			{
				PhysAsset = SkeletalMesh->GetPhysicsAsset();
				SelectedBodySetup = nullptr;
				PhysicsGraphFocusBodySetup = nullptr;
				SelectedConstraintIndex = -1;
				PhysicsGraphNodePositions.clear();
				ViewportClient.SetSelectedBone(SkeletalMesh, SelectedBoneIndex);
				MarkDirty();
			}
		}
		//Save Physics Asset 버튼
		if (PhysAsset)
		{
			const char* SaveLabel = IsDirty() ? "Save Physics Asset*" : "Save Physics Asset";
			if (ImGui::Button(SaveLabel, ImVec2(-1.0f, 0.0f)))
			{
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
				if (FPhysicsAssetManager::Get().Save(PhysAsset, SourcePath))
				{
					ClearDirty();
				}
			}
		}
	}

	//좌측 패널 : 본 계층 구조 트리. 각 본 옆에 해당 본에 바디가 있으면 바디 아이콘 표시. 본/바디 선택 가능. 선택된 본/바디는 우측 세부 패널과 좌측 하단 제약 조건 그래프에 정보 전달. 물리 자산이 있을 때는 제약 조건도 함께 렌더링 (제약 조건 선택 시 세부 패널에 정보 전달). 물리 시뮬레이션이 실행 중일 때는 트리에 시뮬레이션 상태 반영 (예: 아이콘 색상 변경).
	ImGui::Dummy(ImVec2(0.0f, 6.0f));
	ImGui::Text("Bone Hierarchy");
	ImGui::Separator();
	if (SkeletalMesh)
	{
		const FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		const  TArray<UBodySetup*> Bodies = PhysAsset ? PhysAsset->BodySetups : TArray<UBodySetup*>();
		if (Asset)
		{
			for (int32 i = 0; i < static_cast<int32>(Asset->Bones.size()); ++i)
			{
				if (Asset->Bones[i].ParentIndex == -1)
				{
					RenderBoneTreeWithPhysicsAsset(Asset, Bodies, i);
				}
			}
		}
	}
	ImGui::EndChild();

	//좌측 하단 : 선택된 본/바디에 대한 제약 조건 그래프. 본과 바디가 모두 선택되지 않았거나 물리 자산이 없으면 빈 패널.
	ImGui::BeginChild("BodyConstraintGraph", ImVec2(HierarchyWidth, GraphHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove);
	RenderPhysicsAssetGraph(SkeletalMesh, PhysAsset);
	ImGui::EndChild();
	ImGui::EndGroup();

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

	// Right: bone details(본 트리에서 본을 선택했을 때 해당 본의 정보 + 편집 UI, 바디를 선택했을 때는 바디 정보 + 편집 UI, 제약 조건을 선택했을 때는 제약 조건 정보 + 편집 UI). 본/바디/제약 조건이 모두 선택되지 않았을 때는 빈 패널.
	ImGui::BeginChild("BoneDetails", ImVec2(DetailsWidth, 0), true);
	ImGui::Text(SelectedConstraintIndex >= 0 ? "Constraint Details" : (SelectedBodySetup ? "Body Details" : "Bone Details"));
	ImGui::Separator();

	//Constraint Details
	if (SkeletalMesh && PhysAsset && SelectedConstraintIndex >= 0 && SelectedConstraintIndex < static_cast<int32>(PhysAsset->ConstraintSetups.size()))
	{
		FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[SelectedConstraintIndex];
		ImGui::Text("Name: %s", Constraint.ConstraintName.ToString().c_str());
		ImGui::Text("Parent: %s", Constraint.ParentBoneName.ToString().c_str());
		ImGui::Text("Child: %s", Constraint.ChildBoneName.ToString().c_str());
		ImGui::Dummy(ImVec2(0, 8));

		TArray<FPropertyValue> Props;
		CollectEditablePropertiesFromStruct(FConstraintSetup::StaticStruct(), &Constraint, nullptr, Props);
		if (RenderReflectedPropertyTable("##ConstraintReflectionTable", Props, false))
		{
			MarkDirty();
		}

		ImGui::Dummy(ImVec2(0, 8));
		if (ImGui::Button("Delete Constraint", ImVec2(-1.0f, 0.0f)))
		{
			PhysAsset->ConstraintSetups.erase(PhysAsset->ConstraintSetups.begin() + SelectedConstraintIndex);
			SelectedConstraintIndex = -1;
			MarkDirty();
		}
	}
	//Body Details
	else if (SkeletalMesh && SelectedBoneIndex != -1 && SelectedBodySetup)
	{
		FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
		FBone& Bone = Asset->Bones[SelectedBoneIndex];
		const char* ShapeType = nullptr;
		GetFirstPhysicsShapeElem(SelectedBodySetup, &ShapeType);

		ImGui::Text("Body: %s", SelectedBodySetup->GetName().c_str());
		ImGui::Text("Bone: %s", Bone.Name.c_str());
		ImGui::Text("Shape: %s", ShapeType ? ShapeType : "None");
		ImGui::Dummy(ImVec2(0, 10));

		TArray<FPropertyValue> Props;
		SelectedBodySetup->GetEditableProperties(Props);
		if (RenderReflectedPropertyTable("##BodySetupReflectionTable", Props, true))
		{
			ViewportClient.SetSelectedPhysicsBody(SkeletalMesh, SelectedBoneIndex, SelectedBodySetup);
			MarkDirty();
		}
	}
	//Bone Details
	else if (SkeletalMesh && SelectedBoneIndex != -1)
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

//Bone + Body 트리 렌더링. 본과 해당 본에 연결된 바디가 모두 트리 구조로 표현됨. 본 또는 바디를 클릭하면 선택 상태가 업데이트되고 세부 패널이 해당 선택에 맞게 갱신됨. 본을 우클릭하면 해당 본에 새 물리 바디를 추가할 수 있는 컨텍스트 메뉴가 나타남.
void FMeshEditorWidget::RenderBoneTreeWithPhysicsAsset(const FSkeletalMesh* Asset, const TArray<UBodySetup*>& Bodies, int32 Index)
{
	const FBone& Bone = Asset->Bones[Index];
	const FName BoneName(Bone.Name);

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;

	if (Index == SelectedBoneIndex && SelectedBodySetup == nullptr)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	bool bHasChildren = false;
	for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
	{
		if (Asset->Bones[i].ParentIndex == Index)
		{
			bHasChildren = true;
			break;
		}
	}

	TArray<UBodySetup*> BoneBodies;
	for (UBodySetup* Body : Bodies)
	{
		if (Body && Body->GetBoneName() == BoneName)
		{
			BoneBodies.push_back(Body);
		}
	}

	const bool bHasBodyChildren = !BoneBodies.empty();
	if (!bHasChildren && !bHasBodyChildren)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	ImGui::PushID(Index);
	if (ID3D11ShaderResourceView* BoneIcon = LoadEditorIcon(L"Bone.png"))
	{
		ImGui::Image(reinterpret_cast<ImTextureID>(BoneIcon), ImVec2(14.0f, 14.0f));
		ImGui::SameLine(0.0f, 4.0f);
	}
	bool bOpen = ImGui::TreeNodeEx("Bone", Flags, "%s", Bone.Name.c_str());

	if (ImGui::IsItemClicked())
	{
		SelectedBoneIndex = Index;
		SelectedBodySetup = nullptr;
		PhysicsGraphFocusBodySetup = nullptr;
		SelectedConstraintIndex = -1;
		ViewportClient.SetSelectedBone(Cast<USkeletalMesh>(EditedObject), Index);
	}
	if (ImGui::BeginPopupContextItem("##BonePhysicsContext"))
	{
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
		const bool bCanAddBody = SkeletalMesh && BoneBodies.empty();
		if (ImGui::BeginMenu("Add Body", bCanAddBody))
		{
			auto AddBodyMenuItem = [&](const char* Label, EManualPhysicsBodyShape Shape)
			{
				if (ImGui::MenuItem(Label))
				{
					if (UBodySetup* NewBody = AddManualPhysicsBodyForBone(SkeletalMesh, Index, Shape))
					{
						SelectedBoneIndex = Index;
						SelectedBodySetup = NewBody;
						PhysicsGraphFocusBodySetup = NewBody;
						SelectedConstraintIndex = -1;
						ViewportClient.SetSelectedPhysicsBody(SkeletalMesh, Index, NewBody);
						MarkDirty();
					}
				}
			};

			AddBodyMenuItem("Sphere", EManualPhysicsBodyShape::Sphere);
			AddBodyMenuItem("Capsule", EManualPhysicsBodyShape::Capsule);
			AddBodyMenuItem("Cube", EManualPhysicsBodyShape::Box);
			ImGui::EndMenu();
		}
		if (!bCanAddBody)
		{
			ImGui::TextDisabled("Body already exists");
		}
		ImGui::EndPopup();
	}

	if (bOpen && (bHasChildren || bHasBodyChildren))
	{
		for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(BoneBodies.size()); ++BodyIndex)
		{
			UBodySetup* Body = BoneBodies[BodyIndex];
			const FKAggregateGeom& AggGeom = Body->GetAggGeom();
			const int32 ShapeCount = AggGeom.GetElementCount();

			ImGuiTreeNodeFlags BodyFlags = ImGuiTreeNodeFlags_Leaf
				| ImGuiTreeNodeFlags_NoTreePushOnOpen
				| ImGuiTreeNodeFlags_SpanAvailWidth;
			if (SelectedBodySetup == Body)
			{
				BodyFlags |= ImGuiTreeNodeFlags_Selected;
			}

			ImGui::PushID(BodyIndex);
			ImGui::TreeNodeEx("Body", BodyFlags, "Body (%d shapes)", ShapeCount);
			if (ImGui::IsItemClicked())
			{
				SelectedBoneIndex = Index;
				SelectedBodySetup = Body;
				PhysicsGraphFocusBodySetup = Body;
				SelectedConstraintIndex = -1;
				ViewportClient.SetSelectedPhysicsBody(Cast<USkeletalMesh>(EditedObject), Index, Body);
			}
			if (ImGui::BeginPopupContextItem("##BodyPhysicsContext"))
			{
				USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(EditedObject);
				UPhysicsAsset* PhysAsset = SkeletalMesh ? SkeletalMesh->GetPhysicsAsset() : nullptr;
				if (RenderConstraintCandidateMenu(SkeletalMesh, PhysAsset, Body))
				{
					PhysicsGraphFocusBodySetup = Body;
					SelectedConstraintIndex = PhysAsset
						? static_cast<int32>(PhysAsset->ConstraintSetups.size()) - 1
						: -1;
					MarkDirty();
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete Body"))
				{
					if (RemovePhysicsBodyAndConstraints(PhysAsset, Body))
					{
						if (PhysicsGraphFocusBodySetup == Body)
						{
							PhysicsGraphFocusBodySetup = nullptr;
						}
						SelectedBodySetup = nullptr;
						SelectedConstraintIndex = -1;
						ViewportClient.SetSelectedBone(SkeletalMesh, Index);
						MarkDirty();
						ImGui::EndPopup();
						ImGui::PopID();
						ImGui::TreePop();
						ImGui::PopID();
						return;
					}
				}
				ImGui::EndPopup();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Spheres: %d\nBoxes: %d\nCapsules: %d",
					static_cast<int32>(AggGeom.SphereElems.size()),
					static_cast<int32>(AggGeom.BoxElems.size()),
					static_cast<int32>(AggGeom.SphylElems.size()));
			}
			ImGui::PopID();
		}

		for (int32 i = Index + 1; i < static_cast<int32>(Asset->Bones.size()); ++i)
		{
			if (Asset->Bones[i].ParentIndex == Index)
			{
				RenderBoneTreeWithPhysicsAsset(Asset, Bodies, i);
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

bool FMeshEditorWidget::RenderConstraintCandidateMenu(USkeletalMesh* SkeletalMesh, UPhysicsAsset* PhysAsset, UBodySetup* SourceBody)
{
	if (!SkeletalMesh || !PhysAsset || !SourceBody)
	{
		ImGui::MenuItem("Add Constraint", nullptr, false, false);
		return false;
	}

	bool bAdded = false;
	const TArray<FPhysicsConstraintCandidate> Candidates = BuildStrictConstraintCandidates(SkeletalMesh, PhysAsset, SourceBody);
	if (ImGui::BeginMenu("Add Constraint", !Candidates.empty()))
	{
		for (const FPhysicsConstraintCandidate& Candidate : Candidates)
		{
			if (!Candidate.BodySetup)
			{
				continue;
			}

			const FString Label = Candidate.BodySetup->GetBoneName().ToString()
				+ " (" + Candidate.ParentBoneName.ToString() + " -> " + Candidate.ChildBoneName.ToString() + ")";
			if (ImGui::MenuItem(Label.c_str()))
			{
				bAdded = SkeletalMesh->AddPhysicsConstraintBetweenBodies(Candidate.ParentBoneName, Candidate.ChildBoneName);
			}
		}
		ImGui::EndMenu();
	}
	else if (Candidates.empty())
	{
		ImGui::TextDisabled("No strict candidates");
	}

	return bAdded;
}

void FMeshEditorWidget::RenderPhysicsAssetGraph(USkeletalMesh* SkeletalMesh, UPhysicsAsset* PhysAsset)
{
	ImGui::Text("Body-Constraint Graph");
	ImGui::Separator();

	if (!SkeletalMesh || !PhysAsset || PhysAsset->BodySetups.empty())
	{
		ImGui::TextDisabled("No physics bodies.");
		return;
	}

	if (!PhysicsGraphFocusBodySetup && SelectedBodySetup)
	{
		PhysicsGraphFocusBodySetup = SelectedBodySetup;
	}

	UBodySetup* GraphRootBody = PhysicsGraphFocusBodySetup;
	if (!GraphRootBody)
	{
		ImGui::TextDisabled("Select a body to edit its constraint graph.");
		return;
	}

	FSkeletalMesh* Asset = SkeletalMesh->GetSkeletalMeshAsset();
	if (!Asset)
	{
		ImGui::TextDisabled("No skeleton.");
		return;
	}

	const int32 SelectedGraphBoneIndex = FindBoneIndexByName(Asset, GraphRootBody->GetBoneName());
	if (SelectedGraphBoneIndex < 0)
	{
		ImGui::TextDisabled("Selected body bone is missing.");
		return;
	}

	TArray<UBodySetup*> VisibleBodies;
	TArray<int32> VisibleConstraintIndices;
	VisibleBodies.push_back(GraphRootBody);

	auto AddVisibleBody = [&](UBodySetup* BodySetup)
	{
		if (!BodySetup)
		{
			return;
		}

		for (UBodySetup* ExistingBody : VisibleBodies)
		{
			if (ExistingBody == BodySetup)
			{
				return;
			}
		}
		VisibleBodies.push_back(BodySetup);
	};

	const FName SelectedBoneName = GraphRootBody->GetBoneName();
	for (int32 ConstraintIndex = 0; ConstraintIndex < static_cast<int32>(PhysAsset->ConstraintSetups.size()); ++ConstraintIndex)
	{
		const FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[ConstraintIndex];
		if (Constraint.ChildBoneName != SelectedBoneName)
		{
			continue;
		}

		const FName OtherBoneName = Constraint.ParentBoneName;
		const int32 OtherBoneIndex = FindBoneIndexByName(Asset, OtherBoneName);
		if (OtherBoneIndex < 0 || Asset->Bones[SelectedGraphBoneIndex].ParentIndex != OtherBoneIndex)
		{
			continue;
		}

		AddVisibleBody(PhysAsset->FindBodySetupByBoneName(OtherBoneName));
		VisibleConstraintIndices.push_back(ConstraintIndex);
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 CanvasPos = ImGui::GetCursorScreenPos();
	ImVec2 CanvasSize = ImGui::GetContentRegionAvail();
	CanvasSize.x = std::max(CanvasSize.x, 80.0f);
	CanvasSize.y = std::max(CanvasSize.y, 80.0f);

	ImGui::Dummy(CanvasSize);
	const ImVec2 CanvasMax(CanvasPos.x + CanvasSize.x, CanvasPos.y + CanvasSize.y);
	const ImVec2 MousePos = ImGui::GetMousePos();
	const bool bMouseInCanvas =
		MousePos.x >= CanvasPos.x && MousePos.x <= CanvasMax.x &&
		MousePos.y >= CanvasPos.y && MousePos.y <= CanvasMax.y;
	if (bMouseInCanvas && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
	{
		bPhysicsGraphCapturingMouse = true;
		ImGui::SetNextFrameWantCaptureMouse(true);
		InputSystem::Get().SetGuiMouseCapture(true);
	}

	DrawList->AddRectFilled(CanvasPos, CanvasMax, IM_COL32(28, 30, 34, 255));
	DrawList->AddRect(CanvasPos, CanvasMax, IM_COL32(68, 72, 82, 255));

	const float GridStep = 32.0f;
	for (float X = CanvasPos.x + std::fmod(CanvasPos.x, GridStep); X < CanvasMax.x; X += GridStep)
	{
		DrawList->AddLine(ImVec2(X, CanvasPos.y), ImVec2(X, CanvasMax.y), IM_COL32(42, 45, 52, 255));
	}
	for (float Y = CanvasPos.y + std::fmod(CanvasPos.y, GridStep); Y < CanvasMax.y; Y += GridStep)
	{
		DrawList->AddLine(ImVec2(CanvasPos.x, Y), ImVec2(CanvasMax.x, Y), IM_COL32(42, 45, 52, 255));
	}

	const ImVec2 NodeSize(112.0f, 34.0f);
	const ImVec2 ConstraintNodeSize(104.0f, 30.0f);
	TMap<FString, ImVec2> NodeCenters;

	for (int32 BodyIndex = 0; BodyIndex < static_cast<int32>(VisibleBodies.size()); ++BodyIndex)
	{
		UBodySetup* BodySetup = VisibleBodies[BodyIndex];
		if (!BodySetup)
		{
			continue;
		}

		const FString BoneName = BodySetup->GetBoneName().ToString();
		const FString NodeKey = MakePhysicsGraphNodeKey(BodySetup);
		if (PhysicsGraphNodePositions.find(NodeKey) == PhysicsGraphNodePositions.end())
		{
			FPhysicsGraphNodePosition Position;
			Position.X = BodySetup == GraphRootBody ? std::max(12.0f, CanvasSize.x * 0.52f) : 12.0f;
			Position.Y = 16.0f + static_cast<float>(BodyIndex) * 48.0f;
			PhysicsGraphNodePositions[NodeKey] = Position;
		}

		FPhysicsGraphNodePosition& Position = PhysicsGraphNodePositions[NodeKey];
		Position.X = std::clamp(Position.X, 4.0f, std::max(4.0f, CanvasSize.x - NodeSize.x - 4.0f));
		Position.Y = std::clamp(Position.Y, 4.0f, std::max(4.0f, CanvasSize.y - NodeSize.y - 4.0f));
		NodeCenters[BoneName] = ImVec2(CanvasPos.x + Position.X + NodeSize.x * 0.5f, CanvasPos.y + Position.Y + NodeSize.y * 0.5f);
	}

	for (int32 ConstraintListIndex = 0; ConstraintListIndex < static_cast<int32>(VisibleConstraintIndices.size()); ++ConstraintListIndex)
	{
		const int32 ConstraintIndex = VisibleConstraintIndices[ConstraintListIndex];
		const FConstraintSetup& Constraint = PhysAsset->ConstraintSetups[ConstraintIndex];
		const FString ParentName = Constraint.ParentBoneName.ToString();
		const FString ChildName = Constraint.ChildBoneName.ToString();
		auto ParentIt = NodeCenters.find(ParentName);
		auto ChildIt = NodeCenters.find(ChildName);
		if (ParentIt == NodeCenters.end() || ChildIt == NodeCenters.end())
		{
			continue;
		}

		const FString ConstraintNodeKey = MakePhysicsGraphConstraintNodeKey(ConstraintIndex);
		const ImVec2 Mid((ParentIt->second.x + ChildIt->second.x) * 0.5f, (ParentIt->second.y + ChildIt->second.y) * 0.5f);
		if (PhysicsGraphNodePositions.find(ConstraintNodeKey) == PhysicsGraphNodePositions.end())
		{
			FPhysicsGraphNodePosition Position;
			Position.X = Mid.x - CanvasPos.x - ConstraintNodeSize.x * 0.5f;
			Position.Y = Mid.y - CanvasPos.y - ConstraintNodeSize.y * 0.5f;
			PhysicsGraphNodePositions[ConstraintNodeKey] = Position;
		}

		FPhysicsGraphNodePosition& ConstraintPosition = PhysicsGraphNodePositions[ConstraintNodeKey];
		ConstraintPosition.X = std::clamp(ConstraintPosition.X, 4.0f, std::max(4.0f, CanvasSize.x - ConstraintNodeSize.x - 4.0f));
		ConstraintPosition.Y = std::clamp(ConstraintPosition.Y, 4.0f, std::max(4.0f, CanvasSize.y - ConstraintNodeSize.y - 4.0f));
		const ImVec2 ConstraintNodePos(CanvasPos.x + ConstraintPosition.X, CanvasPos.y + ConstraintPosition.Y);
		const ImVec2 ConstraintNodeMax(ConstraintNodePos.x + ConstraintNodeSize.x, ConstraintNodePos.y + ConstraintNodeSize.y);
		const ImVec2 ConstraintCenter(ConstraintNodePos.x + ConstraintNodeSize.x * 0.5f, ConstraintNodePos.y + ConstraintNodeSize.y * 0.5f);

		const ImU32 LineColor = ConstraintIndex == SelectedConstraintIndex
			? IM_COL32(120, 210, 255, 255)
			: IM_COL32(180, 185, 195, 210);
		DrawList->AddLine(ParentIt->second, ConstraintCenter, LineColor, ConstraintIndex == SelectedConstraintIndex ? 3.0f : 2.0f);
		DrawList->AddLine(ConstraintCenter, ChildIt->second, LineColor, ConstraintIndex == SelectedConstraintIndex ? 3.0f : 2.0f);

		ImGui::SetCursorScreenPos(ConstraintNodePos);
		ImGui::PushID(("Constraint" + std::to_string(ConstraintIndex)).c_str());
		ImGui::InvisibleButton("##ConstraintNode", ConstraintNodeSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			bPhysicsGraphCapturingMouse = true;
			ImGui::SetNextFrameWantCaptureMouse(true);
			InputSystem::Get().SetGuiMouseCapture(true);
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			if (!PhysicsGraphFocusBodySetup)
			{
				PhysicsGraphFocusBodySetup = GraphRootBody;
			}
			SelectedConstraintIndex = ConstraintIndex;
			const int32 ChildBoneIndex = FindBoneIndexByName(Asset, Constraint.ChildBoneName);
			if (ChildBoneIndex >= 0)
			{
				SelectedBoneIndex = ChildBoneIndex;
			}
		}
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			bPhysicsGraphCapturingMouse = true;
			ImGui::SetNextFrameWantCaptureMouse(true);
			InputSystem::Get().SetGuiMouseCapture(true);
			const ImVec2 Delta = ImGui::GetIO().MouseDelta;
			ConstraintPosition.X += Delta.x;
			ConstraintPosition.Y += Delta.y;
		}
		if (ImGui::BeginPopupContextItem("##ConstraintContext"))
		{
			if (ImGui::MenuItem("Delete Constraint"))
			{
				PhysAsset->ConstraintSetups.erase(PhysAsset->ConstraintSetups.begin() + ConstraintIndex);
				SelectedConstraintIndex = -1;
				MarkDirty();
				ImGui::EndPopup();
				ImGui::PopID();
				return;
			}
			ImGui::EndPopup();
		}
		DrawList->AddRectFilled(ConstraintNodePos, ConstraintNodeMax, IM_COL32(144, 161, 83, 245), 4.0f);
		DrawList->AddRect(ConstraintNodePos, ConstraintNodeMax,
			ConstraintIndex == SelectedConstraintIndex ? IM_COL32(220, 235, 150, 255) : IM_COL32(185, 196, 130, 255), 4.0f);
		DrawList->AddText(ImVec2(ConstraintNodePos.x + 8.0f, ConstraintNodePos.y + 6.0f), IM_COL32(245, 248, 230, 255), "Constraint");
		DrawList->AddText(ImVec2(ConstraintNodePos.x + 8.0f, ConstraintNodePos.y + 19.0f), IM_COL32(218, 226, 186, 255), ChildName.c_str());
		ImGui::PopID();
	}

	for (UBodySetup* BodySetup : VisibleBodies)
	{
		if (!BodySetup)
		{
			continue;
		}

		const FString NodeKey = MakePhysicsGraphNodeKey(BodySetup);
		FPhysicsGraphNodePosition& Position = PhysicsGraphNodePositions[NodeKey];
		const ImVec2 NodePos(CanvasPos.x + Position.X, CanvasPos.y + Position.Y);
		const ImVec2 NodeMax(NodePos.x + NodeSize.x, NodePos.y + NodeSize.y);
		const bool bSelected = SelectedBodySetup == BodySetup;
		const ImU32 NodeColor = bSelected ? IM_COL32(64, 150, 190, 245) : IM_COL32(70, 74, 82, 245);
		const ImU32 BorderColor = bSelected ? IM_COL32(145, 225, 255, 255) : IM_COL32(115, 120, 132, 255);

		ImGui::SetCursorScreenPos(NodePos);
		ImGui::PushID(BodySetup);
		ImGui::InvisibleButton("##BodyGraphNode", NodeSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			bPhysicsGraphCapturingMouse = true;
			ImGui::SetNextFrameWantCaptureMouse(true);
			InputSystem::Get().SetGuiMouseCapture(true);
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			SelectedBodySetup = BodySetup;
			SelectedConstraintIndex = -1;
			const int32 BoneIndex = FindBoneIndexByName(Asset, BodySetup->GetBoneName());
			if (BoneIndex >= 0)
			{
				SelectedBoneIndex = BoneIndex;
				ViewportClient.SetSelectedPhysicsBody(SkeletalMesh, BoneIndex, BodySetup);
			}
		}
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
		{
			bPhysicsGraphCapturingMouse = true;
			ImGui::SetNextFrameWantCaptureMouse(true);
			InputSystem::Get().SetGuiMouseCapture(true);
			const ImVec2 Delta = ImGui::GetIO().MouseDelta;
			Position.X += Delta.x;
			Position.Y += Delta.y;
		}
		if (ImGui::BeginPopupContextItem("##BodyGraphContext"))
		{
			if (RenderConstraintCandidateMenu(SkeletalMesh, PhysAsset, BodySetup))
			{
				if (!PhysicsGraphFocusBodySetup)
				{
					PhysicsGraphFocusBodySetup = GraphRootBody;
				}
				SelectedConstraintIndex = static_cast<int32>(PhysAsset->ConstraintSetups.size()) - 1;
				MarkDirty();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Delete Body"))
			{
				if (RemovePhysicsBodyAndConstraints(PhysAsset, BodySetup))
				{
					if (PhysicsGraphFocusBodySetup == BodySetup)
					{
						PhysicsGraphFocusBodySetup = nullptr;
					}
					SelectedBodySetup = nullptr;
					SelectedConstraintIndex = -1;
					ViewportClient.SetSelectedBone(SkeletalMesh, SelectedBoneIndex);
					MarkDirty();
					ImGui::EndPopup();
					ImGui::PopID();
					return;
				}
			}
			ImGui::EndPopup();
		}

		DrawList->AddRectFilled(NodePos, NodeMax, NodeColor, 4.0f);
		DrawList->AddRect(NodePos, NodeMax, BorderColor, 4.0f);
		DrawList->AddText(ImVec2(NodePos.x + 8.0f, NodePos.y + 6.0f), IM_COL32(238, 240, 244, 255), BodySetup->GetBoneName().ToString().c_str());
		DrawList->AddText(ImVec2(NodePos.x + 8.0f, NodePos.y + 20.0f), IM_COL32(190, 196, 205, 255), "Body");
		ImGui::PopID();
	}

	ImGui::SetCursorScreenPos(ImVec2(CanvasPos.x, CanvasMax.y));
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh stats overlay
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
