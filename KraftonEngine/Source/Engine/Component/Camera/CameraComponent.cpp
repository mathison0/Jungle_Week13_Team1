#include "Component/Camera/CameraComponent.h"
#include "Component/Camera/SpringArmComponent.h"
#include "Component/Primitive/StaticMeshComponent.h"
#include "Object/Reflection/ObjectFactory.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "GameFramework/GameMode/PlayerController.h"
#include "GameFramework/Camera/PlayerCameraManager.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Engine/Runtime/Engine.h"
#include "Materials/MaterialManager.h"
#include "Mesh/Importer/MeshImportOptions.h"
#include "Mesh/MeshManager.h"
#include <cmath>

void UCameraComponent::BeginPlay()
{
	Super::BeginPlay();

	// E.2/3: PC 가 BeginPlay 시점엔 아직 spawn 전 → PlayerCameraManager nullptr.
	// PC 의 BeginPlay 에서 World 의 모든 카메라 컴포넌트를 catch up 등록하므로 안전.
	if (UWorld* World = GetOwner()->GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CM = PC->GetPlayerCameraManager())
			{
				CM->RegisterCamera(this);
			}
		}
	}
}

void UCameraComponent::EndPlay()
{
	Super::EndPlay();
	if (UWorld* World = GetOwner()->GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (APlayerCameraManager* CM = PC->GetPlayerCameraManager())
			{
				CM->UnregisterCamera(this);
			}
		}
	}
}

void UCameraComponent::CreateRenderState()
{
	EnsureEditorVisualizationMesh();
}

void UCameraComponent::UpdateWorldMatrix() const
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	if (World && World->GetWorldType() == EWorldType::Editor)
	{
		if (USpringArmComponent* SpringArm = Cast<USpringArmComponent>(GetParent()))
		{
			SpringArm->RefreshSpringArm(0.0f, false);
		}
	}

	Super::UpdateWorldMatrix();
}

void UCameraComponent::PreGetEditableProperties()
{
	Super::PreGetEditableProperties();
	EnsureEditorVisualizationMesh();
}

const char* UCameraComponent::GetEditorVisualizationMaterialPath() const
{
	return "Content/Material/Editor/EditorCamera_Blue.mat";
}

UStaticMeshComponent* UCameraComponent::EnsureEditorVisualizationMesh()
{
	if (!Owner)
	{
		return nullptr;
	}

	UWorld* World = Owner->GetWorld();
	if (!World || World->GetWorldType() != EWorldType::Editor)
	{
		return nullptr;
	}

	for (USceneComponent* Child : GetChildren())
	{
		UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Child);
		if (MeshComponent && MeshComponent->IsEditorOnlyComponent())
		{
			MeshComponent->SetHiddenInComponentTree(true);
			MeshComponent->SetCastShadow(false);
			MeshComponent->SetMaterial(0, FMaterialManager::Get().GetOrCreateMaterial(GetEditorVisualizationMaterialPath()));
			MeshComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));
			MeshComponent->SetRelativeScale(FVector(0.01f, 0.01f, 0.01f));
			return MeshComponent;
		}
	}

	if (!GEngine)
	{
		return nullptr;
	}

	// 카메라 기즈모 메시는 import 옵션이 고정이라 프로세스에서 한 번만 로드해 모든 카메라가 공유한다.
	// 옵션 오버로드는 호출마다 캐시를 erase+재import하므로, 월드를 재생성할 때마다(PIE 등)
	// 직전 GPU 버퍼(VB/IB)가 고아로 누수된다. static 캐시로 그 churn 자체를 없앤다.
	static UStaticMesh* SharedCameraVizMesh = nullptr;
	if (!SharedCameraVizMesh)
	{
		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		FImportOptions CameraMeshImportOptions = FImportOptions::Default();
		CameraMeshImportOptions.ForwardAxis = EForwardAxis::Identity;
		CameraMeshImportOptions.WindingOrder = EWindingOrder::Keep;
		SharedCameraVizMesh = FMeshManager::LoadStaticMesh("Content/Data/EditorCamera/CameraMesh.OBJ", CameraMeshImportOptions, Device);
	}
	UStaticMesh* CameraMesh = SharedCameraVizMesh;
	if (!CameraMesh)
	{
		return nullptr;
	}

	UStaticMeshComponent* MeshComponent = Owner->AddComponent<UStaticMeshComponent>();
	if (!MeshComponent)
	{
		return nullptr;
	}

	MeshComponent->AttachToComponent(this);
	MeshComponent->SetEditorOnlyComponent(true);
	MeshComponent->SetHiddenInComponentTree(true);
	MeshComponent->SetStaticMesh(CameraMesh);
	MeshComponent->SetMaterial(0, FMaterialManager::Get().GetOrCreateMaterial(GetEditorVisualizationMaterialPath()));
	MeshComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 90.0f));
	MeshComponent->SetRelativeScale(FVector(0.01f, 0.01f, 0.01f));
	MeshComponent->SetCastShadow(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	return MeshComponent;
}

void UCameraComponent::LookAt(const FVector& Target)
{
	FVector Position = GetWorldLocation();
	FVector Diff = (Target - Position).Normalized();

	constexpr float Rad2Deg = 180.0f / 3.14159265358979f;

	FRotator LookRotation = GetRelativeRotation();
	LookRotation.Pitch = -asinf(Diff.Z) * Rad2Deg;

	if (fabsf(Diff.Z) < 0.999f) {
		LookRotation.Yaw = atan2f(Diff.Y, Diff.X) * Rad2Deg;
	}

	SetRelativeRotation(LookRotation);
}

void UCameraComponent::OnResize(int32 Width, int32 Height)
{
	CameraState.AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
}

void UCameraComponent::SetCameraState(const FCameraState& NewState)
{
	CameraState = NewState;
}

void UCameraComponent::GetCameraView(float /*DeltaTime*/, FMinimalViewInfo& OutPOV) const
{
	UpdateWorldMatrix();
	OutPOV.Location    = GetWorldLocation();
	OutPOV.Rotation    = GetWorldMatrix().ToRotator();
	OutPOV.FOV         = CameraState.FOV;
	OutPOV.AspectRatio = CameraState.AspectRatio;
	OutPOV.OrthoWidth  = CameraState.OrthoWidth;
	OutPOV.NearClip    = CameraState.NearZ;
	OutPOV.FarClip     = CameraState.FarZ;
	OutPOV.bIsOrtho    = CameraState.bIsOrthogonal;
}

void UCameraComponent::GetDepthOfFieldState(FCameraDepthOfFieldState& OutState) const
{
	OutState = FCameraDepthOfFieldState();

	const FDepthOfFieldSettings& DOF = PostProcessSettings.DepthOfField;
	OutState.bEnabled = DOF.bEnableDepthOfField
		&& PostProcessBlendWeight > 0.0f
		&& !CameraState.bIsOrthogonal;

	OutState.DepthOfFieldScale = DOF.DepthOfFieldScale * PostProcessBlendWeight;
	OutState.DepthOfFieldMaxBlurSize = DOF.DepthOfFieldMaxBlurSize;
	OutState.bVisualizeFocusDistance = DOF.bVisualizeFocusDistance;
	OutState.DepthOfFieldFstop = DOF.DepthOfFieldFstop > 0.1f ? DOF.DepthOfFieldFstop : 0.1f;
	OutState.CurrentAperture = OutState.DepthOfFieldFstop;
	OutState.CurrentFocusDistance = DOF.DepthOfFieldFocalDistance > 0.0f ? DOF.DepthOfFieldFocalDistance : 0.0f;

	const float SensorHeight = OutState.SensorHeight > 0.001f ? OutState.SensorHeight : 20.25f;
	const float HalfFOV = CameraState.FOV * 0.5f;
	if (HalfFOV > 0.001f && HalfFOV < 1.5607f)
	{
		OutState.CurrentFocalLength = SensorHeight / (2.0f * tanf(HalfFOV));
	}
	OutState.CurrentHorizontalFOV = 2.0f * atanf(tanf(HalfFOV) * CameraState.AspectRatio);
}
