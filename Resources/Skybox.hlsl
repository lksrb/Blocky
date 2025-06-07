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

// Plot a line on Y using a value between 0.0-1.0
float plot(float2 st)
{
    return smoothstep(0.02, 0.0, abs(st.y - st.x));
}

float4 PSMain(pixel_shader_input In) : SV_Target
{
    // In.Position - screen coords in pixels
    
    float2 UV = (In.WorldPosition.xy + float2(1.0, 1.0)) / 2;
    float T = sin(c_Time * 0.1);
    float2 Resolution = float2(2160, 1185);
    
    // Normalized and flipped
    float2 ST = In.Position.xy / Resolution;
    ST.y = 1.0 - ST.y;
    
     // Define key colors
    float3 Night = float3(0.05, 0.05, 0.2); // dark blue
    float3 Horizon = float3(1.0, 0.4, 0.2); // orange-pink (dawn)
    float3 DawnZenith = float3(0.2, 0.4, 0.8); // light blue (early morning)
    float3 DayZenith = float3(0.4, 0.6, 1.0); // bright daytime blue

    // Vertical gradient factor
    float Gradient = saturate(UV.y);

    // Dawn gradient (zenith to horizon)
    float3 DawnSky = lerp(DawnZenith, Horizon, Gradient);

    // Day gradient (brighter blue sky)
    float3 DaySky = lerp(DayZenith, float3(1.0, 1.0, 1.0), Gradient * 0.1); // Slight gradient toward white at horizon

    // Night Dawn transition
    float DawnBlend = smoothstep(0.10, 0.20, T);

    // Dawn Day transition
    float DayBlend = smoothstep(0.30, 0.50, T);

    // Blend night and dawn
    float3 SkyColor = lerp(Night, DawnSky, DawnBlend);

    // Blend dawn and day sky
    SkyColor = lerp(SkyColor, DaySky, DayBlend);
    
     // Plot a line
    float PCT = plot(ST.xy);
    //SkyColor = (1.0 - PCT) * SkyColor + PCT * float3(1.0, 1.0, 0.0);
    
    return float4(SkyColor, 1.0);
}