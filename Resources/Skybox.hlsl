cbuffer root_constants : register(b0)
{
    column_major float4x4 c_InverseViewProjection;
    float c_Time;
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

    Out.Position = float4(In.Position.xyz, 1.0);
    Out.WorldPosition = mul(c_InverseViewProjection, Out.Position).xyz;

    return Out;
}

float3 dawnColor(float t, float2 uv)
{
    // Define key dawn sky colors
    float3 night = float3(0.05, 0.05, 0.2); // dark blue
    float3 horizon = float3(1.0, 0.4, 0.2); // orange-pink
    float3 zenith = float3(0.2, 0.4, 0.8); // light blue

    // Vertical gradient factor (Y from bottom to top)
    float f = saturate(uv.y);

    // Blend horizon and zenith
    float3 sky = lerp(zenith, horizon, f);

    // Dawn transition blend factor
    float transition = smoothstep(0.20, 0.30, t); // fades in between 0.20 and 0.30 (dawn)
    
    // Mix night and dawn gradient
    return lerp(night, sky, transition);
}

float4 PSMain(pixel_shader_input In) : SV_Target
{
    float3 st = (In.WorldPosition.xyz + float3(1.0, 1.0, 1.0)) / 2;
    //saturate((In.WorldPosition + float3(1.0, 1.0, 1.0)) / 2.0);
    
    float2 resolution = float2(1600, 900);
    
    float3 skyColor = float3(0.2, 0.3, 0.8);
    
    //float2 st = In.Position.xy / resolution;
    
    float3 nightColor = float3(0.05, 0.05, 0.1);
    
    //float3 dawnColor = float3(1.0, 0.4, 0.3);
    float3 dayColor = float3(0.5, 0.7, 1.0);
    float3 duskColor = float3(1.0, 0.3, 0.2);
    
    float y = st.x;
    
    float3 color = float3(y, y, y);
    
    color = lerp(color, skyColor, st.z);
    
    // top-right
    // vec2 tr = step(vec2(0.1),1.0-st);
    // pct *= tr.x * tr.y;
    
    float t = sin(c_Time * 0.5);
    
    color = dawnColor(t, st.xy);
    
    return float4(color, 1.0);
}