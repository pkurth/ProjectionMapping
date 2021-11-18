#include "cs.hlsli"
#include "tracking_icp_common.hlsli"

ConstantBuffer<tracking_icp_reduce_cb> cb			: register(b0);

StructuredBuffer<tracking_indirect> indirectBuffer	: register(t0);
StructuredBuffer<tracking_ata_atb> inputBuffer		: register(t1);
RWStructuredBuffer<tracking_ata_atb> outputBuffer	: register(u0);


static uint bucketize(uint problemSize, uint bucketSize)
{
	return (problemSize + bucketSize - 1) / bucketSize;
}

[numthreads(TRACKING_ICP_BLOCK_SIZE, 1, 1)]
[RootSignature(ICP_REDUCE_RS)]
void main(cs_input IN)
{
	uint numInputs = indirectBuffer[0].counter;
	for (uint i = 0; i < cb.reduceIndex + 1; ++i)
	{
		numInputs = bucketize(numInputs, TRACKING_ICP_BLOCK_SIZE);
	}

	if (IN.dispatchThreadID.x >= numInputs)
	{
		return;
	}

	uint groupIndex = IN.groupIndex;

	g_ata[groupIndex] = inputBuffer[IN.dispatchThreadID.x].ata;
	g_atb[groupIndex] = inputBuffer[IN.dispatchThreadID.x].atb;

	GroupMemoryBarrierWithGroupSync();

	reduce(groupIndex, IN.groupID.x, numInputs, outputBuffer);
}
