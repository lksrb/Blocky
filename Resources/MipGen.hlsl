Texture2D<float4> g_SrcTexture : register(t0);
RWTexture2D<float4> g_DstTexture : register(u0);
SamplerState g_BilinearClamp : register(s0);

cbuffer CB : register(b0)
{
	float2 u_TexelSize;	// 1.0 / destination dimension
}

[numthreads( 8, 8, 1 )]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
	//DTid is the thread ID * the values from numthreads above and in this case correspond to the pixels location in number of pixels.
	//As a result texcoords (in 0-1 range) will point at the center between the 4 pixels used for the mipmap.
    float2 TexCoords = u_TexelSize * (DTid.xy + 0.5);

	//The samplers linear interpolation will mix the four pixel values to the new pixels color
    float4 Color = g_SrcTexture.SampleLevel(g_BilinearClamp, TexCoords, 0);

	//Write the final color into the destination texture.
	g_DstTexture[DTid.xy] = Color;
}