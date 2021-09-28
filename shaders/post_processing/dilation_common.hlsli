#include "cs.hlsli"
#include "post_processing_rs.hlsli"

#define SHARED_MEM_WIDTH (DILATION_BLOCK_SIZE + DILATION_RADIUS * 2)


RWTexture2D<float> output		: register(u0);
Texture2D<float> input			: register(t0);

groupshared float g_values[SHARED_MEM_WIDTH];

[numthreads(NUM_THREADS, 1)]
[RootSignature(DILATION_RS)]
void main(cs_input IN)
{
	const int OTHER_DIM = IN.dispatchThreadID.OTHER_DIM;
	const int tileStart = IN.groupID.MAIN_DIM * DILATION_BLOCK_SIZE;
	const int paddedTileStart = tileStart - DILATION_RADIUS;

	for (int t = IN.groupIndex; t < SHARED_MEM_WIDTH; t += DILATION_BLOCK_SIZE)
	{
		int MAIN_DIM = paddedTileStart + t;
		g_values[t] = input[int2(x, y)];
	}

	GroupMemoryBarrierWithGroupSync();

	float value = 0;
	const int start = IN.groupIndex;
	const int end = IN.groupIndex + DILATION_RADIUS * 2;
	for (int i = start; i <= end; ++i)
	{
		value = max(value, g_values[i]);
	}

	output[IN.dispatchThreadID.xy] = value;
}
