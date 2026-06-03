#pragma once
#include "Object/Reflection/ObjectFactory.h"
#include "Component/SceneComponent.h"
#include "GameFramework/Camera/CameraTypes.h"
#include "Math/MathUtils.h"
#include "Math/Vector.h"

struct FMinimalViewInfo;
class UStaticMeshComponent;

struct FCameraState
{
	float FOV = 3.14159265358979f / 3.0f;
	float AspectRatio = 16.0f / 9.0f;
	float NearZ = 0.1f;
	float FarZ = 1000.0f;
	float OrthoWidth = 10.0f;
	bool bIsOrthogonal = false;
};

#include "Source/Engine/Component/Camera/CameraComponent.generated.h"

UCLASS()
class UCameraComponent : public USceneComponent
{
public:
	GENERATED_BODY()

	UCameraComponent() = default;

	void BeginPlay() override;
	void EndPlay() override;
	void CreateRenderState() override;
	void UpdateWorldMatrix() const override;
	void PreGetEditableProperties() override;
	UStaticMeshComponent* EnsureEditorVisualizationMesh();


	void LookAt(const FVector& Target);
	void SetCameraState(const FCameraState& NewState);
	const FCameraState& GetCameraState() const { return CameraState; }
	const FPostProcessSettings& GetPostProcessSettings() const { return PostProcessSettings; }
	FPostProcessSettings& GetPostProcessSettingsMutable() { return PostProcessSettings; }
	float GetPostProcessBlendWeight() const { return PostProcessBlendWeight; }
	void SetPostProcessBlendWeight(float InWeight) { PostProcessBlendWeight = FMath::Clamp(InWeight, 0.0f, 1.0f); }
	void SetDepthOfFieldEnabled(bool bEnabled) { PostProcessSettings.DepthOfField.bEnableDepthOfField = bEnabled; }
	void SetDepthOfFieldFocalDistance(float InDistance) { PostProcessSettings.DepthOfField.DepthOfFieldFocalDistance = InDistance > 0.0f ? InDistance : 0.0f; }
	void SetDepthOfFieldFstop(float InFstop) { PostProcessSettings.DepthOfField.DepthOfFieldFstop = InFstop > 0.1f ? InFstop : 0.1f; }
	void SetDepthOfFieldScale(float InScale) { PostProcessSettings.DepthOfField.DepthOfFieldScale = FMath::Clamp(InScale, 0.0f, 10.0f); }
	void SetDepthOfFieldMaxBlurSize(float InSize) { PostProcessSettings.DepthOfField.DepthOfFieldMaxBlurSize = InSize > 0.0f ? InSize : 0.0f; }
	void SetDepthOfFieldVisualizeFocusDistance(bool bEnabled) { PostProcessSettings.DepthOfField.bVisualizeFocusDistance = bEnabled; }
	float GetDepthOfFieldFocalDistance() const { return PostProcessSettings.DepthOfField.DepthOfFieldFocalDistance; }
	float GetDepthOfFieldFstop() const { return PostProcessSettings.DepthOfField.DepthOfFieldFstop; }
	float GetDepthOfFieldScale() const { return PostProcessSettings.DepthOfField.DepthOfFieldScale; }
	float GetDepthOfFieldMaxBlurSize() const { return PostProcessSettings.DepthOfField.DepthOfFieldMaxBlurSize; }

	// 카메라 POV 통화 산출 — UE: UCameraComponent::GetCameraView.
	// CameraManager / RenderPipeline 이 이걸 받아 매트릭스/프러스텀을 빌드한다.
	// DeltaTime 은 향후 카메라 lag / interpolation 에 쓰이도록 시그니처 보존.
	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& OutPOV) const;
	virtual void GetDepthOfFieldState(FCameraDepthOfFieldState& OutState) const;
	virtual const char* GetEditorVisualizationMaterialPath() const;

	void SetFOV(float InFOV) { CameraState.FOV = InFOV; }
	void SetOrthoWidth(float InWidth) { CameraState.OrthoWidth = InWidth; }
	void SetOrthographic(bool bOrtho) { CameraState.bIsOrthogonal = bOrtho; }

	void OnResize(int32 Width, int32 Height);

	float GetFOV() const { return CameraState.FOV; }
	float GetNearPlane() const { return CameraState.NearZ; }
	float GetFarPlane() const { return CameraState.FarZ; }
	float GetOrthoWidth() const { return CameraState.OrthoWidth; }
	bool IsOrthogonal() const { return CameraState.bIsOrthogonal; }

private:
	UPROPERTY(Edit, Save, Category="Camera", DisplayName="FOV", Member=CameraState.FOV, Type=Float, Min=0.1f, Max=3.14f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Camera", DisplayName="Near Z", Member=CameraState.NearZ, Type=Float, Min=0.01f, Max=100.0f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Camera", DisplayName="Far Z", Member=CameraState.FarZ, Type=Float, Min=1.0f, Max=100000.0f, Speed=10.0f);
	UPROPERTY(Edit, Save, Category="Camera", DisplayName="Orthographic", Member=CameraState.bIsOrthogonal, Type=Bool);
	UPROPERTY(Edit, Save, Category="Camera", DisplayName="Ortho Width", Member=CameraState.OrthoWidth, Type=Float, Min=0.1f, Max=1000.0f, Speed=0.5f);
	FCameraState CameraState;

	UPROPERTY(Edit, Save, Category="PostProcess", DisplayName="Post Process Blend Weight", Type=Float, Min=0.0f, Max=1.0f, Speed=0.01f)
	float PostProcessBlendWeight = 1.0f;

	UPROPERTY(Edit, Save, Category="PostProcess|Depth of Field", DisplayName="Enable Depth of Field", Member=PostProcessSettings.DepthOfField.bEnableDepthOfField, Type=Bool);
	UPROPERTY(Edit, Save, Category="PostProcess|Depth of Field", DisplayName="Focal Distance", Member=PostProcessSettings.DepthOfField.DepthOfFieldFocalDistance, Type=Float, Min=0.0f, Max=100000.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="PostProcess|Depth of Field", DisplayName="F-stop", Member=PostProcessSettings.DepthOfField.DepthOfFieldFstop, Type=Float, Min=0.1f, Max=64.0f, Speed=0.05f);
	UPROPERTY(Edit, Save, Category="PostProcess|Depth of Field", DisplayName="Scale", Member=PostProcessSettings.DepthOfField.DepthOfFieldScale, Type=Float, Min=0.0f, Max=10.0f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="PostProcess|Depth of Field", DisplayName="Max Blur Size", Member=PostProcessSettings.DepthOfField.DepthOfFieldMaxBlurSize, Type=Float, Min=0.0f, Max=100.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="PostProcess|Depth of Field", DisplayName="Visualize Focus Distance", Member=PostProcessSettings.DepthOfField.bVisualizeFocusDistance, Type=Bool);
	FPostProcessSettings PostProcessSettings;
};
