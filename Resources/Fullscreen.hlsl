 // Composition pass, mostly tonemapping

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
Texture2D<float4> g_BloomTexture : register(t1);
SamplerState g_Sampler : register(s0);

float3 GammaCorrect(float3 Color, float Gamma)
{
    float InverseGamma = 1.0 / Gamma;
    Color = pow(Color, float3(InverseGamma, InverseGamma, InverseGamma));
    return Color;
}

// Based on http://www.oscars.org/science-technology/sci-tech-projects/aces
float3 ACESTonemap(float3 color)
{
    float3x3 m1 = float3x3(
		0.59719, 0.07600, 0.02840,
		0.35458, 0.90834, 0.13383,
		0.04823, 0.01566, 0.83777
	);
    float3x3 m2 = float3x3(
		1.60475, -0.10208, -0.00327,
		-0.53108, 1.10813, -0.07276,
		-0.07367, -0.00605, 1.07602
	);
    float3 v = mul(m1, color);
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(mul(m2, (a / b)), 0.0, 1.0);
}

float3 UpsampleTent9(Texture2D<float4> Texture, SamplerState Sampler, float lod, float2 uv, float2 texelSize, float radius)
{
    float4 offset = texelSize.xyxy * float4(1.0f, 1.0f, -1.0f, 0.0f) * radius;

    // Center
    float3 result = Texture.SampleLevel(Sampler, uv, lod).rgb * 4.0f;

    result += Texture.SampleLevel(Sampler, uv - offset.xy, lod).rgb;
    result += Texture.SampleLevel(Sampler, uv - offset.wy, lod).rgb * 2.0;
    result += Texture.SampleLevel(Sampler, uv - offset.zy, lod).rgb;

    result += Texture.SampleLevel(Sampler, uv + offset.zw, lod).rgb * 2.0;
    result += Texture.SampleLevel(Sampler, uv + offset.xw, lod).rgb * 2.0;

    result += Texture.SampleLevel(Sampler, uv + offset.zy, lod).rgb;
    result += Texture.SampleLevel(Sampler, uv + offset.wy, lod).rgb * 2.0;
    result += Texture.SampleLevel(Sampler, uv + offset.xy, lod).rgb;

    return result * (1.0f / 16.0f);
}

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    const float gamma = 2.2;
    const float pureWhite = 1.0;
    float sampleScale = 0.5;
    
    float BloomIntensity = 1.0;
    float Exposure = 1.0;
    
    float2 BloomTextureSize = float2(0.0, 0.0);
    g_BloomTexture.GetDimensions(BloomTextureSize.x, BloomTextureSize.y);
    
    float3 bloom = UpsampleTent9(g_BloomTexture, g_Sampler, 0, In.UV, 1.0f / BloomTextureSize, sampleScale) * BloomIntensity;
    
    // Composing
    float3 Color = g_MainPassTexture.Sample(g_Sampler, In.UV).rgb;
    Color += bloom;
    Color *= Exposure;
    
    Color = ACESTonemap(Color);
    //Color = GammaCorrect(Color, gamma);
    
    return float4(Color, 1.0f);
}