#include "tracking_rs.hlsli"

groupshared tracking_ata g_ata[TRACKING_ICP_BLOCK_SIZE];
groupshared tracking_atb g_atb[TRACKING_ICP_BLOCK_SIZE];

static void initialize(uint groupIndex)
{
	for (uint i = 0; i < 21; ++i)
	{
		g_ata[groupIndex].m[i] = 0.f;
	}

	for (i = 0; i < 6; ++i)
	{
		g_atb[groupIndex].m[i] = 0.f;
	}
}

static void add(uint a, uint b)
{
	for (uint i = 0; i < 21; ++i)
	{
		g_ata[a].m[i] += g_ata[b].m[i];
	}

	for (i = 0; i < 6; ++i)
	{
		g_atb[a].m[i] += g_atb[b].m[i];
	}
}

static void reduce(uint groupIndex, uint groupID, uint globalCount, RWStructuredBuffer<tracking_ata_atb> outputBuffer)
{
	int numThreadsUpToGroup = (groupID + 1) * TRACKING_ICP_BLOCK_SIZE;
	int diff = max(0, numThreadsUpToGroup - globalCount);
	uint numValidInGroup = TRACKING_ICP_BLOCK_SIZE - diff;

	if (groupIndex < 128 && groupIndex + 128 < numValidInGroup)
	{
		add(groupIndex, groupIndex + 128);
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 64 && groupIndex + 64 < numValidInGroup)
	{
		add(groupIndex, groupIndex + 64);
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 32 && groupIndex + 32 < numValidInGroup)
	{
		add(groupIndex, groupIndex + 32);
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 16 && groupIndex + 16 < numValidInGroup)
	{
		add(groupIndex, groupIndex + 16);
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 8 && groupIndex + 8 < numValidInGroup)
	{
		add(groupIndex, groupIndex + 8);
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 4 && groupIndex + 4 < numValidInGroup)
	{
		add(groupIndex, groupIndex + 4);
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex < 2 && groupIndex + 2 < numValidInGroup)
	{
		add(groupIndex, groupIndex + 2);
	}
	GroupMemoryBarrierWithGroupSync();

	if (groupIndex == 0)
	{
		if (1 < numValidInGroup)
		{
			add(0, 1);
		}
		outputBuffer[groupID].ata = g_ata[0];
		outputBuffer[groupID].atb = g_atb[0];
	}
}
