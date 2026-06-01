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
    float4 center = DepthOfFieldBlurTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float centerCoC = center.a;

    float radiusPixels = saturate(abs(centerCoC)) * max(DepthOfFieldMaxBlurSize, 0.0f);
    float radiusTexels = radiusPixels * 0.5f;
    float2 stepUV = BlurTexelSize * BlurDirection * (radiusTexels * 0.25f);

    float3 color = center.rgb * 0.227027f;

    float weights[4] = { 0.194594f, 0.121622f, 0.054054f, 0.016216f };

    [unroll]
    for (int i = 1; i <= 4; ++i)
    {
        float4 a = DepthOfFieldBlurTexture.SampleLevel(LinearClampSampler, input.uv - stepUV * i, 0);
        float4 b = DepthOfFieldBlurTexture.SampleLevel(LinearClampSampler, input.uv + stepUV * i, 0);
        color += (a.rgb + b.rgb) * weights[i - 1];
    }

    return float4(color, centerCoC);
}
