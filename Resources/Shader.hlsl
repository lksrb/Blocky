cbuffer root_constants : register(b0)
{
    column_major float4x4 ViewProjection;
};

struct vertex_shader_input
{
    float4 Position : POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
    uint TexIndex : TEXINDEX;
};

struct pixel_shader_input
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
    uint TexIndex : TEXINDEX;
};

pixel_shader_input VSMain(vertex_shader_input In)
{
    pixel_shader_input Out;

    Out.Position = mul(ViewProjection, In.Position);
    Out.Color = In.Color;
    Out.TexCoord = In.TexCoord;
    Out.TexIndex = In.TexIndex;

    return Out;
}

// Upper bound
Texture2D<float4> g_Texture[32] : register(t0);
SamplerState g_Sampler : register(s0);

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    float4 Result = In.Color * g_Texture[In.TexIndex].Sample(g_Sampler, In.TexCoord);
    
    if (Result.a == 0.0f)
        discard;
    
    return Result;
}
