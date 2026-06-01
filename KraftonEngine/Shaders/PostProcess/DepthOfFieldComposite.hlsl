#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D<float> DepthOfFieldCoCTexture : register(t27);
Texture2D<float4> DepthOfFieldBlurTexture : register(t28);

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
    float VisualizeFocusDistance;
    float DrawDebugFocusPlane;
    float _Pad0;
    float2 BlurDirection;
    float2 _Pad1;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float4 scene = SceneColorTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float4 blurred = DepthOfFieldBlurTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float coc = DepthOfFieldCoCTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float blurAmount = saturate(abs(coc));

    float focusMask = saturate(1.0f - abs(coc) / 0.035f);
    if (VisualizeFocusDistance > 0.5f)
    {
        float3 nearColor = float3(0.15f, 0.45f, 1.0f);
        float3 farColor = float3(1.0f, 0.35f, 0.15f);
        float3 focusColor = float3(0.1f, 1.0f, 0.25f);
        float3 cocColor = coc < 0.0f ? nearColor : farColor;
        cocColor = lerp(cocColor * blurAmount, focusColor, focusMask);
        return float4(cocColor, 1.0f);
    }

    scene.rgb = lerp(scene.rgb, blurred.rgb, blurAmount);
    if (DrawDebugFocusPlane > 0.5f)
    {
        scene.rgb = lerp(scene.rgb, float3(0.1f, 1.0f, 0.25f), focusMask * 0.85f);
    }
    return scene;
}
