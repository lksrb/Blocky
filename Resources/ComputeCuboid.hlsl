#define FLT_MAX 3.402823466e+38F

cbuffer root_constants : register(b0)
{
    column_major float4x4 in_InverseView;
    column_major float4x4 in_InverseProjection;
    float4 in_CameraPosition;
    uint in_FrameIndex;
};

RWTexture2D<float4> Output : register(u0);
RWBuffer<float4> AccumulatedData : register(u1);

struct ray_payload
{
    float HitDistance;
    float3 WorldPosition;
    float3 WorldNormal;
    int ObjectIndex;
};

struct ray
{
    float3 Origin;
    float3 Direction;
    float3 InvDirection;
};

struct material
{
    float3 Albedo;
    float3 EmissionColor;
    float EmissionPower;
    float Roughness;
};

struct aabb_cube
{
    float3 Position;
    float Size;
    int MaterialIndex;
};

// Globals for convenience
static aabb_cube g_Cubes[2];
static material g_Materials[2];

// Randomness
// Randomness
// Randomness
static uint g_Seed;

uint PCG_Hash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat()
{
    float Scale = 1.0f / (uint) 0xffffffff;
    g_Seed = PCG_Hash(g_Seed);
    return g_Seed * Scale;
}

float3 RandomInUnitSphere()
{
    return normalize(float3(RandomFloat() * 2.0 - 1.0, RandomFloat() * 2.0 - 1.0, RandomFloat() * 2.0 - 1.0));
}

float2 GetCoord(float2 Resolution, uint2 ID)
{
    float2 Coord;
    // Flip it around the X axis
    uint2 FlippedCoord = ID.xy;
    FlippedCoord.y = Resolution.y - FlippedCoord.y;
  
    Coord = 2.0 * (FlippedCoord / Resolution) - 1.0;
    return Coord;
}

float3 ComputeCubeNormal(float3 hitPosition, float3 cubePosition, float g_Cubesize)
{
    float3 localPos = hitPosition - cubePosition;
    float3 halfSize = float3(g_Cubesize, g_Cubesize, g_Cubesize) * 0.5;

    // Normalize into [-1, 1] range
    float3 p = localPos / halfSize;

    float absX = abs(p.x);
    float absY = abs(p.y);
    float absZ = abs(p.z);

    float3 normal = float3(0.0f, 0.0f, 0.0f);

    if (absX > absY && absX > absZ)
        normal = float3((p.x > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
    else if (absY > absX && absY > absZ)
        normal = float3(0.0f, (p.y > 0.0f) ? 1.0f : -1.0f, 0.0f);
    else
        normal = float3(0.0f, 0.0f, (p.z > 0.0f) ? 1.0f : -1.0f);

    return normalize(normal);
}

ray CreateRay(uint2 ID, float2 Coord)
{
    ray Ray;
    float4 Target = mul(in_InverseProjection, float4(Coord.x, Coord.y, 1.0, 1.0));
    Target /= Target.w;
    Ray.Origin = in_CameraPosition.xyz;
    Ray.Direction = mul(in_InverseView, normalize(Target.xyz));
    Ray.InvDirection = 1.0 / Ray.Direction;
    return Ray;
}

ray_payload Miss(ray Ray)
{
    ray_payload Payload;
    Payload.HitDistance = -1;
    return Payload;
}

ray_payload ClosestHit(ray Ray, int ClosestIndex, float HitDistance)
{
    // Otherwise calculate hit position of the closest sphere
    //float3 Hit0 = RayOrigin + RayDirection * T0;
    //float3 HitPosition = Ray.Origin + Ray.Direction * HitDistance;
    //float3 Normal = ComputeCubeNormal(HitPosition, ClosestCube.Position, ClosestCube.Size);

    //float3 LightDir = normalize(float3(-1, -1, -1));

    //float3 ResultColor = ClosestCube.Albedo;
    //float Intensity = max(dot(Normal, -LightDir), 0.0);
    
    //float Ambient = 0.4f;
    //Intensity = saturate(Intensity + Ambient);
    
    //ResultColor *= Intensity;
    //return float4(ResultColor, 1.0);
    
    aabb_cube ClosestCube = g_Cubes[ClosestIndex];
    
    float3 Origin = (Ray.Origin - ClosestCube.Position);
    
    ray_payload Payload;
    Payload.HitDistance = HitDistance;
    Payload.ObjectIndex = ClosestIndex;
    Payload.WorldPosition = Origin + Ray.Direction * HitDistance;
    //Payload.WorldNormal = ComputeCubeNormal(Payload.WorldPosition, ClosestCube.Position, ClosestCube.Size);
    Payload.WorldNormal = normalize(Payload.WorldPosition);
    Payload.WorldPosition += ClosestCube.Position;
    return Payload;
}

ray_payload TraceRay(ray Ray)
{
    int ClosestIndex = -1;
    float HitDistance = FLT_MAX;
    
    for (int i = 0; i < 2; i++)
    {
#if 0
        aabb_cube Current = g_Cubes[i];
        
        float3 HalfSize = float3(Current.Size, Current.Size, Current.Size) * 0.5;
        float3 Min = Current.Position - HalfSize;
        float3 Max = Current.Position + HalfSize;
        
        float3 T0 = (Min - Ray.Origin) * Ray.InvDirection;
        float3 T1 = (Max - Ray.Origin) * Ray.InvDirection;
        
        float3 TMin = min(T0, T1);
        float3 TMax = max(T0, T1);
        
        float TNear = max(max(TMin.x, TMin.y), TMin.z);
        float TFar = min(min(TMax.x, TMax.y), TMax.z);
        
        if (TNear > 0.0f && TNear < TFar && TNear < HitDistance)
        {
            HitDistance = TNear;
            ClosestIndex = i;
        }
#else
        aabb_cube Current = g_Cubes[i];
        
        float3 RayOrigin = Ray.Origin - Current.Position;
        float R = Current.Size;

        float A = dot(Ray.Direction, Ray.Direction);
        float B = 2.0 * dot(RayOrigin, Ray.Direction);
        float C = dot(RayOrigin, RayOrigin) - R * R;

        float Discriminant = B * B - 4.0 * A * C;
      
        if (Discriminant < 0.0)
            continue;
      
        //float TFar = (-B + sqrt(Discriminant)) / (2.0 * A);
        float TNear = (-B - sqrt(Discriminant)) / (2.0 * A);
      
        if (TNear >= 0.0 && TNear < HitDistance)
        {
            HitDistance = TNear;
            ClosestIndex = i;
        }
#endif

    }
    
    // No solution returns background color
    if (ClosestIndex == -1)
    {
        return Miss(Ray);
    }
    
    return ClosestHit(Ray, ClosestIndex, HitDistance);
}

float4 PerPixel(ray Ray)
{
    float3 Light = float3(0.0, 0.0, 0.0);
    float3 Contribution = float3(1.0, 1.0, 1.0);
    
    int BounceCount = 10;
    
    for (int i = 0; i < BounceCount; ++i)
    {
        g_Seed += i;
        
        ray_payload Payload = TraceRay(Ray);
        
        if (Payload.HitDistance < 0.0)
        {
            float3 SkyColor = float3(0.2, 0.3, 0.8);
            //Light += SkyColor * Contribution;
            break;
        }
        
        material Material = g_Materials[g_Cubes[Payload.ObjectIndex].MaterialIndex];
        
        Contribution *= Material.Albedo;
        Light += Material.EmissionPower * Material.EmissionColor;
        
        Ray.Origin = Payload.WorldPosition + Payload.WorldNormal * 0.0001f; // Offset so we dont hit the surface
        Ray.Direction = normalize(Payload.WorldNormal + Material.Roughness * RandomInUnitSphere());
    }
    
    return float4(Light, 1.0);
}

// First we accumulate
[numthreads(4, 4, 1)]
void Accumulate(uint3 ID : SV_DispatchThreadID)
{
    float2 Resolution = float2(1584, 861);
    uint Index = ID.x + ID.y * Resolution.x;

    // Setup Scene
    g_Cubes[0].Position = float3(0.0, -101.0, -1.0);
    g_Cubes[0].Size = 100;
    g_Cubes[0].MaterialIndex = 1;
    
    g_Cubes[1].Position = float3(0.0, 0.0, -3.0);
    g_Cubes[1].Size = 1;
    g_Cubes[1].MaterialIndex = 0;
    
    g_Materials[0].Albedo = float3(0.4, 0.2, 0.8);
    g_Materials[0].Roughness = 0.0;
    g_Materials[0].EmissionColor = float3(0.8f, 0.5f, 0.2f);
    g_Materials[0].EmissionPower = 3.65;
    
    g_Materials[1].Albedo = float3(1.0, 0.0, 1.0);
    g_Materials[1].Roughness = 1.0;
    g_Materials[1].EmissionColor = float3(0.0, 0.0, 0.0);
    g_Materials[1].EmissionPower = 0.0;
    
    int FrameIndex = in_FrameIndex;
    
    AccumulatedData[Index] = float4(0.0, 0.0, 0.0, 0.0);
    for (int Iteration = 0; Iteration < 100; Iteration++)
    {
        // Create seed
        g_Seed = Index * FrameIndex;
    
        // Create ray for target pixel
        ray Ray = CreateRay(ID.xy, GetCoord(Resolution, ID.xy));
        float4 Color = PerPixel(Ray);
        AccumulatedData[Index] += Color;
    
        // Accumulate
        float4 AccumulatedColor = AccumulatedData[Index];
        AccumulatedColor /= (float) FrameIndex;
        AccumulatedColor = saturate(AccumulatedColor);
        Output[ID.xy] = AccumulatedColor;
        
        FrameIndex++;
    }
}

//struct sphere
//{
//    float3 Position;
//    float Radius;
//    float3 Albedo;
//};

//float3 GetRayDirection(uint2 ID)
//{
//    float3 RayDirection = float3(0.0, 0.0, 0.0);

//    float2 Coord = GetCoord(ID);
//    float4 Target = mul(in_InverseProjection, float4(Coord.x, Coord.y, 1.0, 1.0));
//    Target /= Target.w;
//    RayDirection = mul(in_InverseView, normalize(Target.xyz));
//    return RayDirection;
//}

//float4 ProcessSpheres(uint2 ID)
//{
//    sphere Spheres[2];

//    Spheres[0].Position = float3(0.0, 0.0, -1.0);
//    Spheres[0].Albedo = float3(1.0, 0.0, 1.0);
//    Spheres[0].Radius = 0.5;
    
//    Spheres[1].Position = float3(0.0, 0.0, -2.0);
//    Spheres[1].Albedo = float3(0.4, 0.2, 0.8);
//    Spheres[1].Radius = 0.5;

//    int Closest = -1;
//    float HitDistance = FLT_MAX;
//    float3 RayDirection = GetRayDirection(ID.xy);
//    for (int i = 0; i < 2; i++)
//    {
//        sphere Current = Spheres[i];
        
//        float3 RayOrigin = in_CameraPosition.xyz - Current.Position;
//        float R = Current.Radius;

//        float A = dot(RayDirection, RayDirection);
//        float B = 2.0 * dot(RayOrigin, RayDirection);
//        float C = dot(RayOrigin, RayOrigin) - R * R;

//        float Discriminant = B * B - 4.0 * A * C;
        
//        if (Discriminant < 0.0)
//            continue;
        
//        //float TFar = (-B + sqrt(Discriminant)) / (2.0 * A);
//        float TNear = (-B - sqrt(Discriminant)) / (2.0 * A);
        
//        if (TNear >= 0.0 && TNear < HitDistance)
//        {
//            HitDistance = TNear;
//            Closest = i;
//        }
//    }
    
//    // No solution returns background color
//    if (Closest == -1)
//    {
//        return float4(0.2, 0.3, 0.8, 1.0);
//    }
    
//    sphere ClosestSphere = Spheres[Closest];
    
//    float3 RayOrigin = in_CameraPosition.xyz - ClosestSphere.Position;
    
//    // Otherwise calculate hit position of the closest sphere
//    //float3 Hit0 = RayOrigin + RayDirection * T0;
//    float3 Hit1 = RayOrigin + RayDirection * HitDistance;
//    float3 Normal = normalize(Hit1);

//    float3 LightDir = normalize(float3(-1, -1, -1));

//    float3 ResultColor = ClosestSphere.Albedo;
//    float Intensity = max(dot(Normal, -LightDir), 0.0);
    
//    ResultColor *= Intensity;
//    return float4(ResultColor, 1.0);
//}