cbuffer root_constants : register(b0)
{
    column_major float4x4 c_InverseViewProjection;
};

struct vertex_shader_input
{
    float4 Position : POSITION;
};

struct pixel_shader_input
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : WORLDPOSITION;
};

pixel_shader_input VSMain(vertex_shader_input In)
{
    pixel_shader_input Out;

    Out.Position = float4(In.Position.xy, 1.0, 1.0);
    Out.WorldPosition = mul(c_InverseViewProjection, Out.Position).xyz;

    return Out;
}

// Upper bound
TextureCube<float4> g_TextureCube : register(t0);
SamplerState g_SamplerCube : register(s0);

float plot(float2 st)
{
    return smoothstep(0.02, 0.0, abs(st.y - st.x));
}

float4 PSMain(pixel_shader_input In) : SV_Target
{
    float2 st = saturate((In.Position.xy + float2(1.0, 1.0)) / 2.0);
    
    float3 colorA = float3(0.049, 0.041, 0.012);
    float3 colorB = float3(1.000, 0.333, 0.324);
    
    float3 pct = float3(st.y, st.y, st.y);
    
    float3 color = lerp(colorA, colorB, pct);
    
    return float4(color, 1.0);
}