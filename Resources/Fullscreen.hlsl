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

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    float4 Color = g_MainPassTexture.Sample(g_Sampler, In.UV);
    
    //float InverseGamma = 1.0 / 2.2;
    //Color.rgb = pow(Color.rgb, float3(InverseGamma, InverseGamma, InverseGamma));
    
    return Color;
}