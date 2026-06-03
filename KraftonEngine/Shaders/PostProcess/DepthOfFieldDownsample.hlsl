#include "Common/Functions.hlsli"
#include "Common/SystemResources.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D<float> DepthOfFieldCoCTexture : register(t27);

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

struct FDepthOfFieldSplitOutput
{
    float4 FarLayer : SV_TARGET0;
    float4 NearLayer : SV_TARGET1;
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

FDepthOfFieldSplitOutput PS(PS_Input_UV input)
{
    float2 offsets[4] =
    {
        float2(-0.5f, -0.5f),
        float2( 0.5f, -0.5f),
        float2(-0.5f,  0.5f),
        float2( 0.5f,  0.5f)
    };

    float3 farColor = 0.0f;
    float farCoC = 0.0f;
    float3 nearPremultipliedColor = 0.0f;
    float nearCoverage = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 uv = input.uv + offsets[i] * SceneTexelSize;
        float3 color = SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0).rgb;
        float signedCoC = DepthOfFieldCoCTexture.SampleLevel(LinearClampSampler, uv, 0);

        float sampleFarCoC = max(signedCoC, 0.0f);
        float sampleNearCoC = max(-signedCoC, 0.0f);

        farColor += color;
        farCoC = max(farCoC, sampleFarCoC);

        nearPremultipliedColor += color * sampleNearCoC;
        nearCoverage += sampleNearCoC;
    }

    FDepthOfFieldSplitOutput output;
    output.FarLayer = float4(farColor * 0.25f, farCoC);
    output.NearLayer = float4(nearPremultipliedColor * 0.25f, saturate(nearCoverage * 0.25f));
    return output;
}
