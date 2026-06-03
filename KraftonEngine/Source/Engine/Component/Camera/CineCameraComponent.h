#pragma once

#include "Component/Camera/CameraComponent.h"
#include "Core/Types/EngineTypes.h"

UENUM()
enum class ECameraFocusMethod : uint8
{
	Manual,
	Disable,
};

struct FCameraFilmbackSettings
{
	float SensorWidth = 36.0f;
	float SensorHeight = 20.25f;
};

struct FCameraFocusSettings
{
	ECameraFocusMethod FocusMethod = ECameraFocusMethod::Manual;
	float ManualFocusDistance = 3.0f;
	bool bDrawDebugFocusPlane = false;
};

#include "Source/Engine/Component/Camera/CineCameraComponent.generated.h"
struct FCineLetterboxSettings
{
	bool bEnabled = false;
	float Amount = 1.0f;
	float Thickness = 0.12f;
	FLinearColor Color = FLinearColor::Black();
};

UCLASS()
class UCineCameraComponent : public UCameraComponent
{
public:
	GENERATED_BODY()
	UCineCameraComponent() = default;

	void SetLetterboxEnabled(bool bEnabled) { Letterbox.bEnabled = bEnabled; }
	void SetLetterboxAmount(float Amount) { Letterbox.Amount = Amount; }
	void SetLetterboxThickness(float Thickness) { Letterbox.Thickness = Thickness; }
	void SetLetterboxColor(FLinearColor Color) { Letterbox.Color = Color; }

	const FCineLetterboxSettings& GetLetterboxSettings() const { return Letterbox; }
	const FCameraFilmbackSettings& GetFilmback() const { return Filmback; }
	const FCameraFocusSettings& GetFocusSettings() const { return FocusSettings; }

	void SetCurrentFocalLength(float InFocalLength) { CurrentFocalLength = InFocalLength > 0.001f ? InFocalLength : 0.001f; }
	void SetCurrentAperture(float InAperture) { CurrentAperture = InAperture > 0.1f ? InAperture : 0.1f; }
	void SetFocusMethod(ECameraFocusMethod InFocusMethod) { FocusSettings.FocusMethod = InFocusMethod; }
	void SetManualFocusDistance(float InDistance) { FocusSettings.ManualFocusDistance = InDistance > 0.0f ? InDistance : 0.0f; }
	void SetDrawDebugFocusPlane(bool bEnabled) { FocusSettings.bDrawDebugFocusPlane = bEnabled; }

	float GetCurrentFocalLength() const { return CurrentFocalLength; }
	float GetCurrentAperture() const { return CurrentAperture; }
	float GetManualFocusDistance() const { return FocusSettings.ManualFocusDistance; }
	float GetCurrentFocusDistance() const;
	float GetCurrentHorizontalFOV() const;

	void GetCameraView(float DeltaTime, FMinimalViewInfo& OutPOV) const override;
	void GetDepthOfFieldState(FCameraDepthOfFieldState& OutState) const override;
	const char* GetEditorVisualizationMaterialPath() const override;

private:
	UPROPERTY(Edit, Save, Category="Current Camera Settings", DisplayName="Current Focal Length", Type=Float, Min=1.0f, Max=1000.0f, Speed=0.1f)
	float CurrentFocalLength = 17.54f;

	UPROPERTY(Edit, Save, Category="Current Camera Settings", DisplayName="Current Aperture", Type=Float, Min=0.1f, Max=64.0f, Speed=0.05f)
	float CurrentAperture = 5.6f;

	UPROPERTY(Edit, Save, Category="Current Camera Settings", DisplayName="Sensor Width", Member=Filmback.SensorWidth, Type=Float, Min=1.0f, Max=200.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Current Camera Settings", DisplayName="Sensor Height", Member=Filmback.SensorHeight, Type=Float, Min=1.0f, Max=200.0f, Speed=0.1f);
	FCameraFilmbackSettings Filmback;

	UPROPERTY(Edit, Save, Category="Current Camera Settings", DisplayName="Focus Method", Member=FocusSettings.FocusMethod, Enum=ECameraFocusMethod);
	UPROPERTY(Edit, Save, Category="Current Camera Settings", DisplayName="Manual Focus Distance", Member=FocusSettings.ManualFocusDistance, Type=Float, Min=0.0f, Max=100000.0f, Speed=0.1f);
	UPROPERTY(Edit, Save, Category="Current Camera Settings", DisplayName="Draw Debug Focus Plane", Member=FocusSettings.bDrawDebugFocusPlane, Type=Bool);
	FCameraFocusSettings FocusSettings;

	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Enable Letterbox", Member=Letterbox.bEnabled, Type=Bool);
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Letterbox Amount", Member=Letterbox.Amount, Type=Float, Min=0.0f, Max=1.0f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Letterbox Thickness", Member=Letterbox.Thickness, Type=Float, Min=0.0f, Max=0.5f, Speed=0.01f);
	UPROPERTY(Edit, Save, Category="Cinematic", DisplayName="Letterbox Color", Member=Letterbox.Color, Type=Color4);
	FCineLetterboxSettings Letterbox;
};
