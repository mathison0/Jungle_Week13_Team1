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

static const float2 PoissonDisk16[16] =
{
    float2(-0.94201624f, -0.39906216f),
    float2( 0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f),
    float2( 0.34495938f,  0.29387760f),
    float2(-0.91588581f,  0.45771432f),
    float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f,  0.27676845f),
    float2( 0.97484398f,  0.75648379f),
    float2( 0.44323325f, -0.97511554f),
    float2( 0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f),
    float2( 0.79197514f,  0.19090188f),
    float2(-0.24188840f,  0.99706507f),
    float2(-0.81409955f,  0.91437590f),
    float2( 0.19984126f,  0.78641367f),
    float2( 0.14383161f, -0.14100790f)
};

PS_Input_UV VS(uint vertexID : SV_VertexID)
{
    return FullscreenTriangleVS(vertexID);
}

float HashTile(int2 tile)
{
    return frac(sin(dot(float2(tile), float2(12.9898f, 78.233f))) * 43758.5453f);
}

float2 Rotate2D(float2 value, float s, float c)
{
    return float2(value.x * c - value.y * s, value.x * s + value.y * c);
}

float4 PS(PS_Input_UV input) : SV_TARGET
{
    float4 center = DepthOfFieldBlurTexture.SampleLevel(LinearClampSampler, input.uv, 0);
    float centerCoC = center.a;

    bool isNearLayer = DepthOfFieldLayerMode > 0.5f;
    float maxBlur = max(DepthOfFieldMaxBlurSize, 0.0f);
    float radiusPixels = isNearLayer ? maxBlur : saturate(abs(centerCoC)) * maxBlur;
    float radiusTexels = radiusPixels * 0.5f;
    if (radiusTexels <= 0.001f)
    {
        return center;
    }

    int2 tile = int2(input.position.xy) & 3;
    float angle = HashTile(tile) * 6.28318530718f;
    float s = sin(angle);
    float c = cos(angle);

    float centerAbsCoC = abs(centerCoC);
    float4 blurred = center;
    float totalWeight = 1.0f;

    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        float2 disk = Rotate2D(PoissonDisk16[i], s, c);
        float2 sampleUV = input.uv + disk * BlurTexelSize * radiusTexels;
        float4 sampleColor = DepthOfFieldBlurTexture.SampleLevel(LinearClampSampler, sampleUV, 0);

        float sampleAbsCoC = abs(sampleColor.a);
        float sameSide = centerCoC * sampleColor.a >= 0.0f ? 1.0f : 0.0f;
        float cocWeight = lerp(0.35f, 1.0f, sameSide * saturate((sampleAbsCoC + 0.001f) / max(centerAbsCoC, 0.001f)));

        blurred += sampleColor * cocWeight;
        totalWeight += cocWeight;
    }

    return blurred / totalWeight;
}
