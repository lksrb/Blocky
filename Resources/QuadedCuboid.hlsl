#include "Light.hlsl"

cbuffer root_constants : register(b0)
{
    column_major float4x4 c_ViewProjection;
    row_major float4x4 c_ViewMatrix;
    //column_major float4x4 c_LightSpaceMatrix;
};

struct vertex_shader_input
{
    float4 Position : POSITION;
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
    //float4 PositionInLightSpace : POSITIONINLIGHTSPACE;
    float3 Normal : NORMAL;
    float3 ViewPosition : VIEWPOSITION;
    float2 TexCoord : TEXCOORD;
    uint TexIndex : TEXINDEX;
};

pixel_shader_input VSMain(vertex_shader_input In)
{
    pixel_shader_input Out;
    
    Out.Position = mul(c_ViewProjection, In.Position);
    Out.WorldPosition = In.Position;
    Out.Normal = In.Normal;
    Out.ViewPosition = mul(c_ViewMatrix, float4(Out.WorldPosition, 1.0)).xyz;
    Out.Color = In.Color;
    //Out.PositionInLightSpace = mul(c_LightSpaceMatrix, float4(Out.WorldPosition.xyz, 1.0f)); // SHADOWS
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

#define ENABLE_SHADOWS 0

// Upper bound
Texture2D<float4> g_Texture[32] : register(t0);
SamplerState g_Sampler : register(s0);

//Texture2D<float> g_ShadowMap[2] : register(t32);
//SamplerState g_ShadowMapSampler : register(s1);

#if ENABLE_SHADOWS
float ShadowCalculation(float4 ShadowPos, float3 Normal, directional_light Light)
{
    float Shadow = 0.0f;
    
    // perform perspective divide
    ShadowPos.xyz /= ShadowPos.w;
    
    // transform to [0,1] range
    ShadowPos = ShadowPos * 0.5 + 0.5;
    
    // get depth of current fragment from light's perspective
    float CurrentDepth = ShadowPos.z;
    
    if (CurrentDepth > 1.0)
        return 0.0;
    
    // get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float ClosestDepth = g_ShadowMap[0].Sample(g_ShadowMapSampler, ShadowPos.xy);
    
    //float Bias = 0.005;
    float3 LightDir = normalize(-Light.Direction);
    float Bias = max(0.05 * (1.0 - dot(Normal, LightDir)), 0.005);
    //float Bias = 0.02;
    Shadow = CurrentDepth > ClosestDepth ? 1.0 : 0.0;
    
    // check whether current frag pos is in shadow
    //Shadow = CurrentDepth > ClosestDepth ? 1.0 : 0.0;
    return Shadow;
    
}
#endif

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    float3 Normal = normalize(In.Normal);
    float3 ViewDir = normalize(In.ViewPosition - In.WorldPosition);
    
#if ENABLE_SHADOWS
    float ShadowValue = ShadowCalculation(In.PositionInLightSpace, Normal, u_DirectionalLights[0]);
#endif

    float3 TextureColor = g_Texture[In.TexIndex].Sample(g_Sampler, In.TexCoord);
    float3 Result = TextureColor * In.Color.rgb;
    
    // Phase 1: Directional lights
    float Shininess = 32.0;
    for (int i = 0; i < u_DirectionalLightCount; i++)
    {
        //Result += CalculateDirectionalLight(u_DirectionalLights[i], Normal, ViewDir, Shininess, TextureColor * In.Color.rgb);
    }
    
    // Phase 2: Point lights
    for (int j = 0; j < u_PointLightCount; j++)
    {
        //Result += CalculatePointLight(u_PointLights[j], Normal, ViewDir, Shininess, In.WorldPosition, TextureColor * In.Color.rgb);
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