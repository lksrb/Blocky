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

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    //return float4(In.TexCoord, 0.0f, 1.0f);
    return g_texture.Sample(g_sampler, In.TexCoord);
    //return mul(In.Color, );
}
