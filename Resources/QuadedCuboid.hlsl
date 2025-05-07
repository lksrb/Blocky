#include "Light.hlsl"

cbuffer root_constants : register(b0)
{
    column_major float4x4 c_ViewProjection;
    //column_major float4x4 Inversec_ViewProjection;
    row_major float4x4 c_InverseViewMatrix;
};

struct vertex_shader_input
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
    uint TexIndex : TEXINDEX;
};

struct pixel_shader_input
{
    float4 Position : SV_POSITION;
    float3 WorldPosition : WORLDPOSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float3 ViewPosition : VIEWPOSITION;
    float2 TexCoord : TEXCOORD;
    uint TexIndex : TEXINDEX;
};

pixel_shader_input VSMain(vertex_shader_input In)
{
    pixel_shader_input Out;
    
    Out.Position = mul(c_ViewProjection, float4(In.Position, 1.0));
    Out.WorldPosition = In.Position;
    Out.Normal = In.Normal; // Already calculated on CPU side
    Out.ViewPosition = mul(c_InverseViewMatrix, float4(Out.WorldPosition, 1.0)).xyz;
    Out.Color = In.Color;
    Out.TexCoord = In.TexCoord;
    Out.TexIndex = In.TexIndex;

    return Out;
}

// TODO: Reduce the amount of active point lights by calculating which light is visible and which is not
cbuffer light_environment : register(b1)
{
    directional_light u_DirectionalLights[4];
    point_light u_PointLights[64];
    int u_PointLightCount;
    int u_DirectionalLightCount;
};

// Upper bound
Texture2D<float4> g_Texture[32] : register(t0);
SamplerState g_Sampler : register(s0);

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    float3 Normal = normalize(In.Normal);
    float3 ViewDir = normalize(In.ViewPosition - In.WorldPosition.xyz);
    float Shininess = 32.0;

    float3 TextureColor = g_Texture[In.TexIndex].Sample(g_Sampler, In.TexCoord);
    
    // Phase 1: Directional lights
    float3 Result = float3(0, 0, 0);
    for (int i = 0; i < u_DirectionalLightCount; i++)
    {
        Result += CalculateDirectionalLight(u_DirectionalLights[i], Normal, ViewDir, Shininess, TextureColor * In.Color.rgb);
    }
    
    // Phase 2: Point lights
    for (int i = 0; i < u_PointLightCount; i++)
    {
        Result += CalculatePointLight(u_PointLights[i], Normal, ViewDir, Shininess, In.WorldPosition.xyz, TextureColor * In.Color.rgb);
    }
    
    //if (Result.a == 0.0f)
    //    discard;

    return float4(Result, 1.0);
}