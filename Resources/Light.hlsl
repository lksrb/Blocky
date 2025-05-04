#ifndef __LIGHT_HLSL_
#define __LIGHT_HLSL_

struct point_light
{
    float3 Position;
    float3 Radiance;
    float Intensity;
    float FallOff;
    float Radius;
};

struct directional_light
{
    float3 Direction;
    float3 Radiance;
    float Intensity;
};

// Directional light calculation
float3 CalculateDirectionalLights(float3 Normal, float3 ViewDir, float Shininess, float3 TextureColor)
{
    float3 Result = float3(0.0, 0.0, 0.0);

    for (int i = 0; i < 1; i++)
    {
        directional_light Light;
        Light.Direction = float3(1.0, -1.0, 1.0);
        Light.Intensity = 0.5;
        Light.Radiance = float3(1.0, 1.0, 1.0);

        // Calculate direction of the Light source
        float3 LightDir = normalize(-Light.Direction.xyz);

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
        float3 Specular = Light.Intensity * LightSpecular * Light.Radiance * Spec * TextureColor;
        Result += (Ambient + Diffuse + Specular);
    }

    return Result;
}

// Point light calculation
float3 CalculatePointLights(float3 Normal, float3 ViewDir, float Shininess, float3 WorldPosition, float3 TextureColor)
{
    float3 Result = float3(0.0, 0.0, 0.0);

    for (int i = 0; i < 1; i++)
    {
        point_light Light;
        Light.Position = float3(10, 20, 10);
        Light.Radius = 10.0;
        Light.FallOff = 1.0;
        Light.Radiance = float3(1.0, 1.0, 1.0);
        Light.Intensity = 1.0f;

        float3 LightDir = normalize(float3(Light.Position) - WorldPosition);
        float LightDistance = length(float3(Light.Position) - WorldPosition);

        // Calculate diffuse
        float DiffuseAngle = max(dot(Normal, LightDir), 0.0);

        // Calculate specular
        float3 ReflectDir = reflect(-LightDir, Normal); // Phong
        float3 HalfwayDir = normalize(LightDir + ViewDir); // Blinn-Phong
        float Spec = pow(max(dot(Normal, HalfwayDir), 0.0), Shininess);
        
        // Attenuation
        float constant = 1.0;
        float Linear = 0.09;
        float quadratic = 0.032;

        // Calculate attenuation
        float Attenuation = clamp(1.0 - (LightDistance * LightDistance) / (Light.Radius * Light.Radius), 0.0, 1.0);
        Attenuation *= lerp(Attenuation, 1.0, Light.FallOff);

        // TODO: Materials
        float3 LightAmbient = float3(0.05, 0.05, 0.05);
        float3 LightDiffuse = float3(0.8, 0.8, 0.8);
        float3 LightSpecular = float3(1.0, 1.0, 1.0);

        // Combine Results
        float3 Ambient = Light.Intensity * LightAmbient * TextureColor;
        float3 Diffuse = Light.Radiance * Light.Intensity * LightDiffuse * DiffuseAngle * TextureColor;
        float3 Specular = Light.Radiance * Light.Intensity * LightSpecular * Spec;

        Ambient *= Attenuation;
        Diffuse *= Attenuation;
        Specular *= Attenuation;

        // Specular is weird due to the texture color not being a specular one
        Result += (Ambient + Diffuse);
    }
    
    return Result;
}

#endif // __LIGHT_HLSL_