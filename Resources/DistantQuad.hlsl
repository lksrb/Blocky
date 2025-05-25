cbuffer root_constants : register(b0)
{
    column_major float4x4 c_MVPNoTranslation;
};

struct vertex_shader_input
{
    float4 Position : POSITION;
};

struct pixel_shader_input
{
    float4 Position : SV_POSITION;
};

pixel_shader_input VSMain(vertex_shader_input In)
{
    pixel_shader_input Out;

    Out.Position = mul(c_MVPNoTranslation, In.Position);

    return Out;
}

float4 PSMain(pixel_shader_input In) : SV_Target
{
    return float4(1.0, 1.0, 1.0, 1.0);
}