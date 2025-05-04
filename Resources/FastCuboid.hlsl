cbuffer root_constants : register(b0)
{
    column_major float4x4 ViewProjection;
};

struct vertex_shader_input
{
    // Per Vertex
    float4 Position : POSITION;
    //float3 Normal : NORMAL;

    // Per instance
    float4 TransformRow0 : TRANSFORMA;
    float4 TransformRow1 : TRANSFORMB;
    float4 TransformRow2 : TRANSFORMC;
    float4 TransformRow3 : TRANSFORMD;
    
    float2 TextureCoord0 : TEXCOORD0;
    float2 TextureCoord1 : TEXCOORD1;
    float2 TextureCoord2 : TEXCOORD2;
    float2 TextureCoord3 : TEXCOORD3;

    float2 TextureCoord4 : TEXCOORD4;
    float2 TextureCoord5 : TEXCOORD5;
    float2 TextureCoord6 : TEXCOORD6;
    float2 TextureCoord7 : TEXCOORD7;

    float2 TextureCoord8 : TEXCOORD8;
    float2 TextureCoord9 : TEXCOORD9;
    float2 TextureCoord10 : TEXCOORD10;
    float2 TextureCoord11 : TEXCOORD11;

    float2 TextureCoord12 : TEXCOORD12;
    float2 TextureCoord13 : TEXCOORD13;
    float2 TextureCoord14 : TEXCOORD14;
    float2 TextureCoord15 : TEXCOORD15;

    float2 TextureCoord16 : TEXCOORD16;
    float2 TextureCoord17 : TEXCOORD17;
    float2 TextureCoord18 : TEXCOORD18;
    float2 TextureCoord19 : TEXCOORD19;

    float2 TextureCoord20 : TEXCOORD20;
    float2 TextureCoord21 : TEXCOORD21;
    float2 TextureCoord22 : TEXCOORD22;
    float2 TextureCoord23 : TEXCOORD23;
    
    float4 Color : COLOR;
    uint TexIndex : TEXINDEX;
};

struct pixel_shader_input
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
    uint TexIndex : TEXINDEX;
};

pixel_shader_input VSMain(vertex_shader_input In, uint VertexID : SV_VertexID)
{
    pixel_shader_input Out;
    
    float2 TextureCoord[] = { 
        In.TextureCoord0, In.TextureCoord1, In.TextureCoord2, In.TextureCoord3, In.TextureCoord4, In.TextureCoord5, 
        In.TextureCoord6, In.TextureCoord7, In.TextureCoord8, In.TextureCoord9, In.TextureCoord10, In.TextureCoord11,
        In.TextureCoord12, In.TextureCoord13, In.TextureCoord14, In.TextureCoord15, In.TextureCoord16, In.TextureCoord17,
        In.TextureCoord18, In.TextureCoord19, In.TextureCoord20, In.TextureCoord21, In.TextureCoord22, In.TextureCoord23
    };

    float4x4 Transform = float4x4(In.TransformRow0, In.TransformRow1, In.TransformRow2, In.TransformRow3);
    
    Out.Position = mul(ViewProjection, mul(transpose(Transform), In.Position));
    Out.Color = In.Color;
    Out.TexCoord = TextureCoord[VertexID];
    Out.TexIndex = In.TexIndex;

    return Out;
}

// Upper bound
Texture2D<float4> g_Texture[32] : register(t0);
SamplerState g_Sampler : register(s0);

float4 PSMain(pixel_shader_input In) : SV_TARGET
{
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
    
    float4 Result = In.Color * g_Texture[In.TexIndex].Sample(g_Sampler, In.TexCoord);
    
    if (Result.a == 0.0f)
        discard;

    return Result;
}