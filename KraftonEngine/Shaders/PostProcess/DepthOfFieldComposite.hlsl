#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D<float> DepthOfFieldCoCTexture : register(t27);
Texture2D<float4> DepthOfFieldFarBlurTexture : register(t28);
Texture2D<float4> DepthOfFieldNearBlurTexture : register(t29);

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

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float GetLayerAmount(float normalizedCoC)
{
    float effectiveCoCPixels = saturate(normalizedCoC) * max(DepthOfFieldMaxBlurSize, 0.0f);
    float focusTransition = max(DepthOfFieldFocusTransitionPixels, 0.001f);
    return smoothstep(0.0f, focusTransition, effectiveCoCPixels);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float4 scene = SceneColorTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float4 farBlur = DepthOfFieldFarBlurTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float4 nearBlur = DepthOfFieldNearBlurTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float signedCoC = DepthOfFieldCoCTexture.SampleLevel(LinearClampSampler, input.uv, 0);

    float farAmount = GetLayerAmount(max(signedCoC, 0.0f));
    float nearAmount = GetLayerAmount(max(-signedCoC, 0.0f));
    float focusMask = 1.0f - max(farAmount, nearAmount);

    if (VisualizeFocusDistance > 0.5f)
    {
        float3 nearColor = float3(0.15f, 0.45f, 1.0f);
        float3 farColor = float3(1.0f, 0.35f, 0.15f);
        float3 focusColor = float3(0.1f, 1.0f, 0.25f);
        float layerAmount = max(farAmount, nearAmount);
        float3 cocColor = signedCoC < 0.0f ? nearColor : farColor;
        cocColor = lerp(cocColor * layerAmount, focusColor, focusMask);
        return float4(cocColor, 1.0f);
    }

    scene.rgb = lerp(scene.rgb, farBlur.rgb, farAmount);

    float nearAlpha = saturate(nearBlur.a);
    float3 nearColorUnpremultiplied = nearAlpha > 0.001f ? nearBlur.rgb / nearAlpha : scene.rgb;
    scene.rgb = lerp(scene.rgb, nearColorUnpremultiplied, nearAlpha);

    if (DrawDebugFocusPlane > 0.5f)
    {
        scene.rgb = lerp(scene.rgb, float3(0.1f, 1.0f, 0.25f), focusMask * 0.85f);
    }
    return scene;
}
