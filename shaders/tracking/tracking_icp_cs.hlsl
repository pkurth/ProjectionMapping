#include "cs.hlsli"
#include "tracking_icp_common.hlsli"

StructuredBuffer<tracking_indirect> indirectBuffer			: register(t0);
StructuredBuffer<tracking_correspondence> correspondences	: register(t1);
RWStructuredBuffer<tracking_ata_atb> outputBuffer			: register(u0);


[numthreads(TRACKING_ICP_BLOCK_SIZE, 1, 1)]
[RootSignature(ICP_RS)]
void main(cs_input IN)
{
	uint counter = indirectBuffer[0].counter;
	if (IN.dispatchThreadID.x >= counter)
	{
		return;
	}

	tracking_correspondence correspondence = correspondences[IN.dispatchThreadID.x];
	float4 grad0 = correspondence.grad0;
	float4 grad1 = correspondence.grad1;

	uint groupIndex = IN.groupIndex;

	g_ata[groupIndex].m[ata_m00] = grad0.x * grad0.x;
	g_ata[groupIndex].m[ata_m01] = grad0.x * grad0.y;
	g_ata[groupIndex].m[ata_m02] = grad0.x * grad0.z;
	g_ata[groupIndex].m[ata_m03] = grad0.x * grad1.x;
	g_ata[groupIndex].m[ata_m04] = grad0.x * grad1.y;
	g_ata[groupIndex].m[ata_m05] = grad0.x * grad1.z;

	//g_ata[groupIndex].m[ata_m10] = grad0.y * grad0.x;
	g_ata[groupIndex].m[ata_m11] = grad0.y * grad0.y;
	g_ata[groupIndex].m[ata_m12] = grad0.y * grad0.z;
	g_ata[groupIndex].m[ata_m13] = grad0.y * grad1.x;
	g_ata[groupIndex].m[ata_m14] = grad0.y * grad1.y;
	g_ata[groupIndex].m[ata_m15] = grad0.y * grad1.z;

	//g_ata[groupIndex].m[ata_m20] = grad0.z * grad0.x;
	//g_ata[groupIndex].m[ata_m21] = grad0.z * grad0.y;
	g_ata[groupIndex].m[ata_m22] = grad0.z * grad0.z;
	g_ata[groupIndex].m[ata_m23] = grad0.z * grad1.x;
	g_ata[groupIndex].m[ata_m24] = grad0.z * grad1.y;
	g_ata[groupIndex].m[ata_m25] = grad0.z * grad1.z;

	//g_ata[groupIndex].m[ata_m30] = grad1.x * grad0.x;
	//g_ata[groupIndex].m[ata_m31] = grad1.x * grad0.y;
	//g_ata[groupIndex].m[ata_m32] = grad1.x * grad0.z;
	g_ata[groupIndex].m[ata_m33] = grad1.x * grad1.x;
	g_ata[groupIndex].m[ata_m34] = grad1.x * grad1.y;
	g_ata[groupIndex].m[ata_m35] = grad1.x * grad1.z;

	//g_ata[groupIndex].m[ata_m40] = grad1.y * grad0.x;
	//g_ata[groupIndex].m[ata_m41] = grad1.y * grad0.y;
	//g_ata[groupIndex].m[ata_m42] = grad1.y * grad0.z;
	//g_ata[groupIndex].m[ata_m43] = grad1.y * grad1.x;
	g_ata[groupIndex].m[ata_m44] = grad1.y * grad1.y;
	g_ata[groupIndex].m[ata_m45] = grad1.y * grad1.z;

	//g_ata[groupIndex].m[ata_m50] = grad1.z * grad0.x;
	//g_ata[groupIndex].m[ata_m51] = grad1.z * grad0.y;
	//g_ata[groupIndex].m[ata_m52] = grad1.z * grad0.z;
	//g_ata[groupIndex].m[ata_m53] = grad1.z * grad1.x;
	//g_ata[groupIndex].m[ata_m54] = grad1.z * grad1.y;
	g_ata[groupIndex].m[ata_m55] = grad1.z * grad1.z;


	g_atb[groupIndex].m[0] = grad0.x * grad0.w;
	g_atb[groupIndex].m[1] = grad0.y * grad0.w;
	g_atb[groupIndex].m[2] = grad0.z * grad0.w;
	g_atb[groupIndex].m[3] = grad1.x * grad0.w;
	g_atb[groupIndex].m[4] = grad1.y * grad0.w;
	g_atb[groupIndex].m[5] = grad1.z * grad0.w;


	GroupMemoryBarrierWithGroupSync();

	reduce(groupIndex, IN.groupID.x, counter, outputBuffer);
}
