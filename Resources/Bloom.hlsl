//
// Bloom shader
//

cbuffer root_constants : register(b0)
{
    float4 c_Params;
    float c_LOD;
    int c_Mode; // 0 = prefilter, 1 = downsample, 2 = firstUpsample, 3 = upsample
};

// This texture is used was writing
RWTexture2D<float4> o_Image : register(u0);

Texture2D<float4> g_Texture : register(t0);

Texture2D<float4> g_BloomTexture : register(t1);

SamplerState g_Sampler : register(s0);

#define MODE_PREFILTER      0
#define MODE_DOWNSAMPLE     1
#define MODE_UPSAMPLE_FIRST 2
#define MODE_UPSAMPLE       3

// Quadratic color thresholding
// curve = (threshold - knee, knee * 2, 0.25 / knee)
float4 QuadraticThreshold(float4 color, float threshold, float3 curve)
{
    const float Epsilon = 1.0e-4;
    
    // Maximum pixel brightness
    float brightness = max(max(color.r, color.g), color.b);
    // Quadratic curve
    float rq = clamp(brightness - curve.x, 0.0, curve.y);
    rq = (rq * rq) * curve.z;
    color *= max(rq, brightness - threshold) / max(brightness, Epsilon);
    return color;
}

float4 Prefilter(float4 color, float2 uv)
{
    float threshold = 1.0;
    float knee = 0.1;
    
    float4 Params = float4(threshold, threshold - knee, knee * 2.0, 0.25 / knee);
    
    float clampValue = 20.0f;
    color = min(float(clampValue), color);
    color = QuadraticThreshold(color, Params.x, Params.yzw);
    return color;
}

float3 DownsampleBox13(Texture2D<float4> Texture, SamplerState Sampler, float lod, float2 uv, float2 texelSize)
{
    // Center
    float3 A = Texture.SampleLevel(Sampler, uv, lod).rgb;

    texelSize *= 0.5f; // Sample from center of texels

    // Inner box
    float3 B = Texture.SampleLevel(Sampler, uv + texelSize * float2(-1.0f, -1.0f), lod).rgb;
    float3 C = Texture.SampleLevel(Sampler, uv + texelSize * float2(-1.0f, 1.0f), lod).rgb;
    float3 D = Texture.SampleLevel(Sampler, uv + texelSize * float2(1.0f, 1.0f), lod).rgb;
    float3 E = Texture.SampleLevel(Sampler, uv + texelSize * float2(1.0f, -1.0f), lod).rgb;
    
    // Outer box
    float3 F = Texture.SampleLevel(Sampler, uv + texelSize * float2(-2.0f, -2.0f), lod).rgb;
    float3 G = Texture.SampleLevel(Sampler, uv + texelSize * float2(-2.0f, 0.0f), lod).rgb;
    float3 H = Texture.SampleLevel(Sampler, uv + texelSize * float2(0.0f, 2.0f), lod).rgb;
    float3 I = Texture.SampleLevel(Sampler, uv + texelSize * float2(2.0f, 2.0f), lod).rgb;
    float3 J = Texture.SampleLevel(Sampler, uv + texelSize * float2(2.0f, 2.0f), lod).rgb;
    float3 K = Texture.SampleLevel(Sampler, uv + texelSize * float2(2.0f, 0.0f), lod).rgb;
    float3 L = Texture.SampleLevel(Sampler, uv + texelSize * float2(-2.0f, -2.0f), lod).rgb;
    float3 M = Texture.SampleLevel(Sampler, uv + texelSize * float2(0.0f, -2.0f), lod).rgb;

    // Weights
    float3 result = float3(0.0, 0.0, 0.0);
    
    // Inner box
    result += (B + C + D + E) * 0.5f;
    // Bottom-left box
    result += (F + G + A + M) * 0.125f;
    // Top-left box
    result += (G + H + I + A) * 0.125f;
    // Top-right box
    result += (A + I + J + K) * 0.125f;
    // Bottom-right box
    result += (M + A + K + L) * 0.125f;

    // 4 samples each
    result *= 0.25f;

    return result;
}

[numthreads(4, 4, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    // DTid.xy is the pixel coordinate
    
    uint2 TextureSize = uint2(0, 0);
    o_Image.GetDimensions(TextureSize.x, TextureSize.y);
    
    float2 UV = float2(float(DTid.x) / TextureSize.x, float(DTid.y) / TextureSize.y);
    UV += (1.0f / TextureSize) * 0.5f;
    
    float4 Color = float4(1.0, 0.0, 1.0, 1.0);
    if (c_Mode == MODE_PREFILTER)
    {
        Color.rgb = DownsampleBox13(g_Texture, g_Sampler, 0, UV, 1.0f / TextureSize);
        Color = Prefilter(Color, UV);
        Color.a = 1.0f;
    }
    else if (c_Mode == MODE_DOWNSAMPLE)
    {
        // Downsample
        Color.rgb = DownsampleBox13(g_Texture, g_Sampler, c_LOD, UV, 1.0f / TextureSize);
    }
    
    o_Image[DTid.xy] = Color;
}