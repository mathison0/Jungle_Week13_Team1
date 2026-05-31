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

float SelectMaxAbsCoC(float currentCoC, float candidateCoC)
{
    return abs(candidateCoC) > abs(currentCoC) ? candidateCoC : currentCoC;
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float2 offsets[4] =
    {
        float2(-0.5f, -0.5f),
        float2( 0.5f, -0.5f),
        float2(-0.5f,  0.5f),
        float2( 0.5f,  0.5f)
    };

    float3 color = 0.0f;
    float coc = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 uv = input.uv + offsets[i] * SceneTexelSize;
        color += SceneColorTexture.SampleLevel(LinearClampSampler, uv, 0).rgb;
        coc = SelectMaxAbsCoC(coc, DepthOfFieldCoCTexture.SampleLevel(LinearClampSampler, uv, 0));
    }

    return float4(color * 0.25f, coc);
}
