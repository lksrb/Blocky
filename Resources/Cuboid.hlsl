#include "Light.hlsl"

cbuffer root_constants : register(b0)
{
    column_major float4x4 ViewProjectionMatrix;
    row_major float4x4 c_ViewMatrix;
    column_major float4x4 c_LightSpaceMatrix;
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
    float Emission : EMISSION;
};

struct pixel_shader_input
{
    float4 Position : SV_POSITION;
    float4 WorldPosition : WORLDPOSITION;
    float4 Color : COLOR;
    float4 PositionInLightSpace : POSITIONINLIGHTSPACE;
    float3 Normal : NORMAL;
    float3 ViewPosition : VIEWPOSITION;
    float2 TexCoord : TEXCOORD;
    uint TexIndex : TEXINDEX;
    float Emission : EMISSION;
};

pixel_shader_input VSMain(vertex_shader_input In)
{
    pixel_shader_input Out;
    
    float4x4 Transform = transpose(float4x4(In.TransformRow0, In.TransformRow1, In.TransformRow2, In.TransformRow3));
    
    Out.Position = mul(ViewProjectionMatrix, mul(Transform, In.VertexPosition));
    Out.WorldPosition = mul(Transform, In.VertexPosition);
    Out.Normal = In.Normal; // There is no need for transforming normals since blocks are axis-aligned
    Out.ViewPosition = mul(c_ViewMatrix, Out.WorldPosition).xyz;
    Out.Color = In.Color;
    Out.PositionInLightSpace = mul(c_LightSpaceMatrix, float4(Out.WorldPosition.xyz, 1.0f)); // SHADOWS
    Out.TexCoord = In.TexCoord;
    Out.TexIndex = In.TexIndex;
    Out.Emission = In.Emission;

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

#define ENABLE_SHADOWS 1

// Upper bound
Texture2D<float4> g_Texture[32] : register(t0);
SamplerState g_Sampler : register(s0);

Texture2D<float> g_ShadowMap[2] : register(t32);
SamplerState g_ShadowMapSampler : register(s1);

float ShadowCalculation(float4 ShadowPos, float3 Normal, directional_light Light)
{
    float Shadow = 0.0f;
#if ENABLE_SHADOWS
    // perform perspective divide
    ShadowPos.xyz /= ShadowPos.w;
    
    // transform to [0,1] range
    ShadowPos = ShadowPos * 0.5 + 0.5;
    
    // get depth of current fragment from light's perspective
    float CurrentDepth = ShadowPos.z;
    
    if (CurrentDepth > 1.0)
        return 0.0;
    
    float2 FlippedShadowPos = float2(ShadowPos.x, 1.0 - ShadowPos.y);
    
    // get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float ClosestDepth = g_ShadowMap[0].Sample(g_ShadowMapSampler, FlippedShadowPos);
    
    //float Bias = 0.005;
    float3 LightDir = normalize(-Light.Direction);
    //float Bias = max(0.05 * (1.0 - dot(Normal, LightDir)), 0.005);
    float Bias = 0.005;
    Shadow = CurrentDepth - Bias > ClosestDepth ? 1.0 : 0.0;
#endif
    return Shadow;
}

#if ENABLE_SHADOWS
// Directional light calculation
float3 CalculateDirectionalLight2(directional_light Light, float3 Normal, float3 ViewDir, float Shininess, float3 TextureColor, float Shadow)
{
    float3 Result = float3(0.0, 0.0, 0.0);

    // Calculate direction of the Light source
    float3 LightDir = normalize(-Light.Direction);

    // Calculate diffuse 
    float DiffuseAngle = max(dot(Normal, LightDir), 0.0);

    // Calculate specular 
    float3 ReflectDir = reflect(-LightDir, Normal);
    float Spec = pow(max(dot(ViewDir, ReflectDir), 0.0), Shininess);

    // TODO: Materials
    float3 LightAmbient = float3(0.5, 0.5, 0.5);
    float3 LightDiffuse = float3(0.8, 0.8, 0.8);
    float3 LightSpecular = float3(1.0, 1.0, 1.0);

    // Combine results
    float3 Ambient = Light.Intensity * LightAmbient * Light.Radiance * TextureColor;
    float3 Diffuse = Light.Intensity * LightDiffuse * Light.Radiance * DiffuseAngle * TextureColor;
    //float3 Specular = Light.Intensity * LightSpecular * Light.Radiance * Spec * TextureColor;
    float3 Specular = float3(0.0, 0.0, 0.0);
    Result += (Ambient + (1.0 - Shadow) * (Diffuse + Specular));

    return Result;
}
#endif

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    float3 Normal = normalize(In.Normal);
    float3 ViewDir = normalize(In.ViewPosition - In.WorldPosition.xyz);
    float Shininess = 32.0;
    float ShadowValue = ShadowCalculation(In.PositionInLightSpace, Normal, u_DirectionalLights[0]);
    float3 TextureColor = g_Texture[In.TexIndex].Sample(g_Sampler, In.TexCoord);
    
    float3 Result = float3(0.0, 0.0, 0.0);
    
    // Phase 1: Directional lights
    for (int i = 0; i < u_DirectionalLightCount; i++)
    {
#if ENABLE_SHADOWS
        Result += CalculateDirectionalLight(u_DirectionalLights[i], Normal, ViewDir, Shininess, TextureColor * In.Color.rgb);
#else
        Result += CalculateDirectionalLight2(u_DirectionalLights[i], Normal, ViewDir, Shininess, TextureColor * In.Color.rgb); 
#endif
    }
    
    // Phase 2: Point lights
    for (int j = 0; j < u_PointLightCount; j++)
    {
        Result += CalculatePointLight(u_PointLights[j], Normal, ViewDir, Shininess, In.WorldPosition.xyz, TextureColor * In.Color.rgb);
    }
    
    // Emissive material
    Result += TextureColor * In.Color.rgb * In.Emission;
    
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