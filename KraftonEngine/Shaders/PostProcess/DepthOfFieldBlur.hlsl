#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

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

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float4 center = DepthOfFieldBlurTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float centerCoC = center.a;

    bool isNearLayer = DepthOfFieldLayerMode > 0.5f;
    float maxBlur = max(DepthOfFieldMaxBlurSize, 0.0f);
    float radiusPixels = isNearLayer ? maxBlur : saturate(abs(centerCoC)) * maxBlur;
    float radiusTexels = radiusPixels * 0.5f;
    float2 stepUV = BlurTexelSize * BlurDirection * (radiusTexels * 0.25f);

    float4 blurred = center * 0.227027f;

    float weights[4] = { 0.194594f, 0.121622f, 0.054054f, 0.016216f };

    [unroll]
    for (int i = 1; i <= 4; ++i)
    {
        float4 a = DepthOfFieldBlurTexture.SampleLevel(LinearClampSampler, input.uv - stepUV * i, 0);
        float4 b = DepthOfFieldBlurTexture.SampleLevel(LinearClampSampler, input.uv + stepUV * i, 0);
        blurred += (a + b) * weights[i - 1];
    }

    return blurred;
}
