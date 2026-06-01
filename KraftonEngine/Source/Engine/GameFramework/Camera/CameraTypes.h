#pragma once

#include "Core/Types/CoreTypes.h"
#include "Core/Types/EngineTypes.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"

// ============================================================
// EViewTargetBlendFunction — SetViewTargetWithBlend 의 블렌딩 함수
// UE: EViewTargetBlendFunction
// ============================================================
enum class EViewTargetBlendFunction : uint8
{
	VTBlend_Linear,
	VTBlend_Cubic,
	VTBlend_EaseIn,
	VTBlend_EaseOut,
	VTBlend_EaseInOut,
	VTBlend_PreBlended,
};

// ============================================================
// FViewTargetTransitionParams — view target 전환 파라미터
// UE: FViewTargetTransitionParams
// ============================================================
struct FViewTargetTransitionParams
{
	float BlendTime = 0.0f;
	EViewTargetBlendFunction BlendFunction = EViewTargetBlendFunction::VTBlend_Linear;
	float BlendExp = 0.0f;
	bool bLockOutgoing = false;
};

// ============================================================
// ECameraShakePlaySpace — Shake 가 적용되는 좌표 공간
// UE: ECameraShakePlaySpace
// ============================================================
enum class ECameraShakePlaySpace : uint8
{
	CameraLocal,    // 카메라 로컬
	World,          // 월드 좌표
	UserDefined,    // UserPlaySpaceRot 기준
};

// ============================================================
// FCameraShakeUpdateResult — 매 프레임 Shake 결과 (additive)
// UE: FCameraShakeUpdateResult (간소화)
// ============================================================
struct FCameraShakeUpdateResult
{
	FVector Location = FVector(0.0f, 0.0f, 0.0f);   // additive world-space offset
	FRotator Rotation;                              // additive Pitch/Yaw/Roll (degrees)
	float FOV = 0.0f;                               // additive radians (FCameraState/FMinimalViewInfo 단위와 통일)
};

struct FCameraFadeState
{
	bool bEnabled = false;
	float Amount = 0.0f;
	FLinearColor Color = FLinearColor::Black();
	bool bFadeAudio = false;
};

struct FCameraVignetteState
{
	bool bEnabled = false;
	float Intensity = 0.0f;
	float Radius = 0.75f;
	float Softness = 0.35f;
	FLinearColor Color = FLinearColor::Black();
};

struct FCameraLetterboxState
{
	bool bEnabled = false;
	float Amount = 1.0f;
	float Thickness = 0.12f;
	FLinearColor Color = FLinearColor::Black();
};

struct FDepthOfFieldSettings
{
	bool bEnableDepthOfField = false;

	float DepthOfFieldFstop = 5.6f;
	float DepthOfFieldFocalDistance = 3.0f;
	float DepthOfFieldScale = 1.0f;
	float DepthOfFieldMaxBlurSize = 12.0f;
	bool bVisualizeFocusDistance = false;
};

struct FPostProcessSettings
{
	FDepthOfFieldSettings DepthOfField;
};

struct FCameraDepthOfFieldState
{
	bool bEnabled = false;

	float DepthOfFieldFstop = 5.6f;
	float DepthOfFieldScale = 1.0f;
	float DepthOfFieldMaxBlurSize = 12.0f;
	bool bVisualizeFocusDistance = false;
	bool bDrawDebugFocusPlane = false;

	float SensorWidth = 36.0f;
	float SensorHeight = 20.25f;
	float CurrentAperture = 5.6f;
	float CurrentFocalLength = 17.54f;
	float CurrentFocusDistance = 3.0f;
	float CurrentHorizontalFOV = 0.0f;
};
