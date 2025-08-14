struct pixel_shader_input
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

pixel_shader_input VSMain(uint VertexID : SV_VertexID)
{
    pixel_shader_input Out;
    float2 uv = float2((VertexID << 1) & 2, VertexID & 2);
    Out.Position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    Out.UV = uv;
    return Out;
}

Texture2D<float4> g_MainPassTexture : register(t0);
SamplerState g_Sampler : register(s0);

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


float3 GammaCorrect(float3 Color)
{
    float InverseGamma = 1.0 / 2.2;
    Color = pow(Color, float3(InverseGamma, InverseGamma, InverseGamma));
    return Color;
}

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    float4 Color = g_MainPassTexture.Sample(g_Sampler, In.UV);
    
    uint2 TextureSize = uint2(0, 0);
    g_MainPassTexture.GetDimensions(TextureSize.x, TextureSize.y);
    
    Color.rgb = DownsampleBox13(g_MainPassTexture, g_Sampler, 0, In.UV, 1.0f / TextureSize);
    Color = Prefilter(Color, In.UV);
    Color.a = 1.0f;
    
    return Color;
}