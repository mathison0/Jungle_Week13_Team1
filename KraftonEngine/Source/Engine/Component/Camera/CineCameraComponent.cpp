#include "Component/Camera/CineCameraComponent.h"

#include "Object/Reflection/ObjectFactory.h"
#include "Render/Types/MinimalViewInfo.h"
#include "Serialization/Archive.h"

#include <cmath>

float UCineCameraComponent::GetCurrentFocusDistance() const
{
	switch (FocusSettings.FocusMethod)
	{
	case ECameraFocusMethod::Disable:
		return 0.0f;
	case ECameraFocusMethod::Manual:
	default:
		return FocusSettings.ManualFocusDistance;
	}
}

float UCineCameraComponent::GetCurrentHorizontalFOV() const
{
	const float FocalLength = CurrentFocalLength > 0.001f ? CurrentFocalLength : 0.001f;
	return 2.0f * atanf(Filmback.SensorWidth / (2.0f * FocalLength));
}

void UCineCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& OutPOV) const
{
	Super::GetCameraView(DeltaTime, OutPOV);

	const float FocalLength = CurrentFocalLength > 0.001f ? CurrentFocalLength : 0.001f;
	const float SensorHeight = Filmback.SensorHeight > 0.001f ? Filmback.SensorHeight : 0.001f;
	OutPOV.FOV = 2.0f * atanf(SensorHeight / (2.0f * FocalLength));
}

void UCineCameraComponent::GetDepthOfFieldState(FCameraDepthOfFieldState& OutState) const
{
	Super::GetDepthOfFieldState(OutState);

	const float FocusDistance = GetCurrentFocusDistance();

	OutState.SensorWidth = Filmback.SensorWidth;
	OutState.SensorHeight = Filmback.SensorHeight;
	OutState.CurrentAperture = CurrentAperture;
	OutState.CurrentFocalLength = CurrentFocalLength;
	OutState.CurrentFocusDistance = FocusDistance;
	OutState.CurrentHorizontalFOV = GetCurrentHorizontalFOV();
	OutState.DepthOfFieldFstop = CurrentAperture;
	OutState.bDrawDebugFocusPlane = FocusSettings.bDrawDebugFocusPlane;

	if (FocusSettings.FocusMethod == ECameraFocusMethod::Disable)
	{
		OutState.bEnabled = false;
		return;
	}
}

const char* UCineCameraComponent::GetEditorVisualizationMaterialPath() const
{
	return "Content/Material/Editor/EditorCineCamera_Black.mat";
}
