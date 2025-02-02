#if 0
// Vertex shader
// Vertex shader
// Vertex shader

struct ModelViewProjection
{
    matrix MVP;
};

ConstantBuffer<ModelViewProjection> ModelViewProjectionCB : register(b0);

struct VertexPosColor
{
    float3 Position : POSITION;
    float3 Color    : COLOR;
};

struct VertexShaderOutput
{
	float4 Color    : COLOR;
    float4 Position : SV_Position;
};

VertexShaderOutput VSMain(VertexPosColor IN)
{
    VertexShaderOutput OUT;
  
    OUT.Position = mul(ModelViewProjectionCB.MVP, float4(IN.Position, 1.0f));
    OUT.Color = float4(IN.Color, 1.0f);

    return OUT;
}

// Pixel shader
// Pixel shader
// Pixel shader

struct PixelShaderInput
{
    float4 Color : COLOR;
};

float4 PSMain(PixelShaderInput IN) : SV_Target
{
    return IN.Color;
}
#else

cbuffer RootConstants : register(b0)
{
    column_major float4x4 ViewProjection;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = mul(ViewProjection, position);
    result.color = color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
#endif
