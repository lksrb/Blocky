#include "Light.hlsl"

cbuffer root_constants : register(b0)
{
    column_major float4x4 ViewProjectionMatrix;
    //column_major float4x4 InverseViewProjection;
    row_major float4x4 c_ViewMatrix;
};

struct vertex_shader_input
{
    float4 VertexPosition : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;

    // Per instance
    float4 TransformRow0 : TRANSFORMA;
    float4 TransformRow1 : TRANSFORMB;
    float4 TransformRow2 : TRANSFORMC;
    float4 TransformRow3 : TRANSFORMD;
    float4 Color : COLOR;
    uint TexIndex : TEXINDEX;
};

struct pixel_shader_input
{
    float4 Position : SV_POSITION;
    float4 WorldPosition : WORLDPOSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float3 ViewPosition : VIEWPOSITION;
    float2 TexCoord : TEXCOORD;
    uint TexIndex : TEXINDEX;
};

pixel_shader_input VSMain(vertex_shader_input In)
{
    pixel_shader_input Out;
    
    float4x4 Transform = float4x4(In.TransformRow0, In.TransformRow1, In.TransformRow2, In.TransformRow3);
    
    Out.Position = mul(ViewProjectionMatrix, mul(transpose(Transform), In.VertexPosition));
    Out.WorldPosition = mul(transpose(Transform), In.VertexPosition);
    Out.Normal = In.Normal; // There is no need for transforming normals since blocks are axis-aligned
    Out.ViewPosition = mul(c_ViewMatrix, Out.WorldPosition).xyz;
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
    for (int j = 0; j < u_PointLightCount; j++)
    {
        Result += CalculatePointLight(u_PointLights[j], Normal, ViewDir, Shininess, In.WorldPosition.xyz, TextureColor * In.Color.rgb);
    }
    
#if 0
    //float4 Result = In.Color;
    //float4 Result = float4(In.TexCoord, 0.0f, 1.0f);
    
    float2 atlasSize; // size in pixels
    g_Texture[In.TexIndex].GetDimensions(atlasSize.x, atlasSize.y);

    float2 texelSize = 1.0 / atlasSize;
    float2 uv = In.TexCoord;

    // Tile size in UVs
    float2 tileSize = float2(1.0 / 64.0, 1.0 / 32.0);

    // Get tile offset (can be passed as a constant or precalculated)
    float2 tileOffset = floor(uv / tileSize) * tileSize;

    // Clamp within tile with half-texel margin
    float2 clampedUV = clamp(
        uv,
        tileOffset + texelSize * 0.5,
        tileOffset + tileSize - texelSize * 0.5
    );
#endif
    
    //if (Result.a == 0.0f)
    //    discard;

    return float4(Result, 1.0);
}