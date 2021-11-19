#include "cs.hlsli"
#include "tracking_rs.hlsli"

ConstantBuffer<tracking_prepare_dispatch_cb> cb			: register(b0);
RWStructuredBuffer<tracking_indirect> indirectBuffer	: register(u0);

static uint bucketize(uint problemSize, uint bucketSize)
{
	return (problemSize + bucketSize - 1) / bucketSize;
}

static D3D12_DISPATCH_ARGUMENTS createArguments(uint problemSize, uint maximum)
{
	D3D12_DISPATCH_ARGUMENTS result;
	if (problemSize < maximum)
	{
		result.ThreadGroupCountX = 0;
		result.ThreadGroupCountY = 0;
		result.ThreadGroupCountZ = 0;
	}
	else
	{
		result.ThreadGroupCountX = bucketize(problemSize, TRACKING_ICP_BLOCK_SIZE);
		result.ThreadGroupCountY = 1;
		result.ThreadGroupCountZ = 1;
	}
	return result;
}

[numthreads(1, 1, 1)]
[RootSignature(PREPARE_DISPATCH_RS)]
void main(cs_input IN)
{
	tracking_indirect indirect = indirectBuffer[0];
	uint counter = indirect.counter;

	indirect.initialICP = createArguments(counter, cb.minNumCorrespondences);
	indirect.reduce0 = createArguments(indirect.initialICP.ThreadGroupCountX, 2);
	indirect.reduce1 = createArguments(indirect.reduce0.ThreadGroupCountX, 2);

	indirectBuffer[0] = indirect;
}
