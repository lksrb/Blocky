// Vertex shader
// Vertex shader
// Vertex shader
#type vertex
#version 450 core

// Per vertex
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TextureCoord;
layout(location = 3) in uint a_TextureIndex;
layout(location = 4) in vec2 a_TextureTiling;

layout(push_constant) uniform PC_Camera
{
    mat4 pc_ViewProjection;
};

layout(location = 0) out flat uint o_TextureIndex;
layout(location = 1) out VertexOutput
{
	vec4 o_Color;
    vec2 o_TextureCoord;
};

void main()
{
    o_Color = a_Color;
    o_TextureCoord = a_TextureCoord * a_TextureTiling;
	o_TextureIndex = a_TextureIndex;
    gl_Position = pc_ViewProjection * vec4(a_Position, 1.0);
}

// Fragment shader
// Fragment shader
// Fragment shader
#type fragment
#version 450 core

// Inputs
layout(location = 0) in flat uint in_TextureIndex;
layout(location = 1) in VertexInput
{
	vec4 in_Color;
    vec2 in_TextureCoord;
};

// Textures
layout(binding = 0) uniform sampler2D u_Textures[32];

// Outputs
layout(location = 0) out vec4 o_Color;

void main()
{
    o_Color = in_Color * texture(u_Textures[in_TextureIndex], in_TextureCoord);

	// Temporary solution for blending semi-transparent objects
	if(o_Color.w == 0.0f)
		discard;
}