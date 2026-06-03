#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"

cbuffer DepthOfFieldCB : register(b2)
{
    float2 SceneTexelSize;
    float2 BlurTexelSize;
    float FocusDistanceMM;
    float FocalLengthMM;
    float FStop;
    float SensorHeightMM;
    float NearClip;
    float FarClip;
    float RenderTargetHeight;
    float DepthOfFieldScale;
    float DepthOfFieldMaxBlurSize;
    float DepthOfFieldAcceptableCoCPixels;
    float DepthOfFieldFocusTransitionPixels;
    float VisualizeFocusDistance;
    float DrawDebugFocusPlane;
    float DepthOfFieldLayerMode;
    float2 BlurDirection;
    float2 _Pad1;
    float2 _Pad2;
};

static const float WorldUnitsToMillimeters = 1000.0f;

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float ComputeSignedCircleOfConfusionMM(float sceneDepthMM)
{
    float f = max(FocalLengthMM, 0.001f);
    float s = max(FocusDistanceMM, f + 1.0f);
    float z = max(sceneDepthMM, 1.0f);
    float N = max(FStop, 0.1f);
    return (f * f) / (N * max(s - f, 1.0f)) * ((z - s) / z);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    int2 coord = int2(input.position.xy);
    float deviceDepth = SceneDepthTexture.Load(int3(coord, 0));

    float sceneDepth = NearClip * FarClip / (NearClip - deviceDepth * (NearClip - FarClip));
    float sceneDepthMM = sceneDepth * WorldUnitsToMillimeters;

    float signedCoCMM = ComputeSignedCircleOfConfusionMM(sceneDepthMM);
    float signedCoCPixels = signedCoCMM / max(SensorHeightMM, 0.001f) * RenderTargetHeight;
    signedCoCPixels *= DepthOfFieldScale;

    float maxBlur = max(DepthOfFieldMaxBlurSize, 0.0f);
    float acceptableCoC = max(DepthOfFieldAcceptableCoCPixels, 0.0f);
    float effectiveCoCPixels = max(abs(signedCoCPixels) - acceptableCoC, 0.0f);
    float signedEffectiveCoCPixels = signedCoCPixels < 0.0f ? -effectiveCoCPixels : effectiveCoCPixels;
    float coc = maxBlur > 0.0f ? clamp(signedEffectiveCoCPixels, -maxBlur, maxBlur) / maxBlur : 0.0f;
    return float4(coc, 0.0f, 0.0f, 1.0f);
}
