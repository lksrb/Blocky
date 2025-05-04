cbuffer root_constants : register(b0)
{
    column_major float4x4 ViewProjection;
    //column_major float4x4 InverseViewProjection;
    column_major float4x4 InverseViewMatrix;
};

struct vertex_shader_input
{
    float4 Position : POSITION;
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
    float4 WorldPosition : SV_POSITION;
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
    
    Out.WorldPosition = mul(ViewProjection, mul(transpose(Transform), In.Position));
    //Out.Color = float4(In.Normal, 1.0);
    Out.Normal = In.Normal; // There is no need for transforming normals since blocks are axis-aligned
    
    Out.ViewPosition = mul(InverseViewMatrix, Out.WorldPosition).xyz;
    Out.Color = In.Color;
    Out.TexCoord = In.TexCoord;
    Out.TexIndex = In.TexIndex;

    return Out;
}

struct directional_light
{
    float3 Direction;
    float3 Radiance;
    float Intensity;
};

// Upper bound
Texture2D<float4> g_Texture[32] : register(t0);
SamplerState g_Sampler : register(s0);

// Directional light calculation
float3 CalculateDirectionalLights(float3 normal, float3 viewDir, float shininess, float3 TextureColor)
{
    float3 result = float3(0.0, 0.0, 0.0);

    for (int i = 0; i < 1; i++)
    {
        directional_light light;
        light.Direction = float3(1.0, -1.0, 1.0);
        light.Intensity = 1.0;
        light.Radiance = float3(1.0, 1.0, 1.0);

        // Calculate direction of the light source
        float3 lightDir = normalize(-light.Direction.xyz);

        // Calculate diffuse
        float diffuseAngle = max(dot(normal, lightDir), 0.0);

        // Calculate specular 
        float3 reflectDir = reflect(-lightDir, normal);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);

        // TODO: Materials
        float3 lightAmbient = float3(0.5, 0.5, 0.5);
        float3 lightDiffuse = float3(0.8, 0.8, 0.8);
        float3 lightSpecular = float3(1.0, 1.0, 1.0);

        // Combine results
        float3 ambient = light.Intensity * lightAmbient * light.Radiance * TextureColor;
        float3 diffuse = light.Intensity * lightDiffuse * light.Radiance * diffuseAngle * TextureColor;
        float3 specular = light.Intensity * lightSpecular * light.Radiance * spec * TextureColor;// * float3(texture(u_MaterialTexture[1], Input.TexCoord));
        result += (ambient + diffuse + specular);
    }

    return result;
}

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
    float3 normal = normalize(In.Normal);
    float3 viewDir = normalize(In.ViewPosition - In.WorldPosition.xyz);
    float shininess = 32.0;

    float3 TextureColor = g_Texture[In.TexIndex].Sample(g_Sampler, In.TexCoord);
    
    // Phase 1: Directional light
    float3 Result = CalculateDirectionalLights(normal, viewDir, shininess, TextureColor * In.Color.xyz);
    
    //float4 Result = In.Color * g_Texture[In.TexIndex].Sample(g_Sampler, In.TexCoord);
    //float4 Result = In.Color;
    //float4 Result = float4(In.TexCoord, 0.0f, 1.0f);

    //if (Result.a == 0.0f)
    //    discard;

    return float4(Result, 1.0);
}