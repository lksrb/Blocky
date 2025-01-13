#type vertex
#version 450 core

// Per vertex
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

layout(push_constant) uniform PC_Camera
{
    mat4 pc_ViewProjection;
};

layout(location = 0) out VertexOutput
{
	vec4 o_Color;
};

void main()
{
    o_Color = a_Color;
    gl_Position = pc_ViewProjection * vec4(a_Position, 1.0);
}

// -----------------------------------------------------------------------------
// Fragment shader
#type fragment
#version 450 core

// Inputs
layout(location = 0) in VertexInput
{
	vec4 in_Color;
};

// Outputs
layout(location = 0) out vec4 o_Color;

void main()
{
    o_Color = in_Color;
}