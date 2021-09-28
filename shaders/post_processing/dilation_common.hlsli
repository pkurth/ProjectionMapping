#include "cs.hlsli"
#include "post_processing_rs.hlsli"

#define SHARED_MEM_WIDTH (DILATION_BLOCK_SIZE + DILATION_MAX_RADIUS * 2)

ConstantBuffer<dilation_cb> cb	: register(b0);
RWTexture2D<float> output		: register(u0);
Texture2D<float> input			: register(t0);

groupshared float g_values[SHARED_MEM_WIDTH];

[numthreads(NUM_THREADS, 1)]
[RootSignature(DILATION_RS)]
void main(cs_input IN)
{
	const int radius = cb.radius;
	const int actualSharedMemoryWidth = DILATION_BLOCK_SIZE + radius * 2;

	const int OTHER_DIM = IN.dispatchThreadID.OTHER_DIM;
	const int tileStart = IN.groupID.MAIN_DIM * DILATION_BLOCK_SIZE;
	const int paddedTileStart = tileStart - radius;

	for (int t = IN.groupIndex; t < actualSharedMemoryWidth; t += DILATION_BLOCK_SIZE)
	{
		int MAIN_DIM = paddedTileStart + t;
		g_values[t] = input[int2(x, y)];
	}

	GroupMemoryBarrierWithGroupSync();

	float value = 0;
	const int start = IN.groupIndex;
	const int end = IN.groupIndex + radius * 2;
	for (int i = start; i <= end; ++i)
	{
		value = max(value, g_values[i]);
	}

	output[IN.dispatchThreadID.xy] = value;
}
