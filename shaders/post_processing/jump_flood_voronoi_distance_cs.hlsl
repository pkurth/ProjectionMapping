#include "cs.hlsli"
#include "post_processing_rs.hlsli"

Texture2D<float4> input				: register(t0);
RWTexture2D<float2> output			: register(u0);


[numthreads(POST_PROCESSING_BLOCK_SIZE, POST_PROCESSING_BLOCK_SIZE, 1)]
[RootSignature(JUMP_FLOOD_VORONOI_DISTANCE_RS)]
void main(cs_input IN)
{
	int2 tc = IN.dispatchThreadID.xy;

	float4 i = input[tc];

	float2 xy1 = i.xy;
	float2 xy2 = i.zw;

	float2 center = (float2)tc;

	float l1 = 0.f;
	float l2 = 0.f;

	if (xy1.x != 0.f && xy1.y != 0.f)
	{
		l1 = length(xy1 - center);
	}
	if (xy2.x != 0.f && xy2.y != 0.f)
	{
		l2 = length(xy2 - center);
	}

	output[tc] = float2(l1, l2);
}
