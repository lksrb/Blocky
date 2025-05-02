RWTexture2D<float4> output : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    output[DTid.xy] = float4(1.0, 0.0, 0.0, 1.0); // Red
}