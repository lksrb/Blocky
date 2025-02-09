cbuffer root_constants : register(b0)
{
    column_major float4x4 ViewProjection;
};

struct vertex_shader_input
{
    float4 Position : POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};

struct pixel_shader_input
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};

pixel_shader_input VSMain(vertex_shader_input In)
{
    pixel_shader_input Out;

    Out.Position = mul(ViewProjection, In.Position);
    Out.Color = In.Color;
    Out.TexCoord = In.TexCoord;

    return Out;
}

Texture2D<float4> g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    float4 Result = In.Color * g_Texture.Sample(g_Sampler, In.TexCoord);
    
    if (Result.a == 0.0f)
        discard;
    
    return Result;
}
