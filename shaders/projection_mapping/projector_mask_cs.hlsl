#include "cs.hlsli"
#include "projector_rs.hlsli"


ConstantBuffer<projector_mask_cb> cb				: register(b0, space0);
ConstantBuffer<projector_mask_common_cb> common		: register(b1, space0);
StructuredBuffer<projector_cb> projectors			: register(t0, space0);

Texture2D<float> depthTextures[32]					: register(t0, space1);
Texture2D<float2> discontinuityDistanceFields[32]	: register(t0, space2);
Texture2D<float> bestMasks[32]						: register(t0, space3);

RWTexture2D<float2> output[32]						: register(u0, space0);

SamplerState clampSampler							: register(s0);


[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
[RootSignature(PROJECTOR_MASK_RS)]
void main(cs_input IN)
{
	uint index = cb.index;

	uint2 texCoord = IN.dispatchThreadID.xy;
	float2 dimensions = projectors[index].screenDims;
	if (texCoord.x >= (uint)dimensions.x || texCoord.y >= (uint)dimensions.y)
	{
		return;
	}

	const float depth = depthTextures[index][texCoord];
	if (depth == 1.f)
	{
		output[index][texCoord] = (float2)0.f;
		return;
	}

	const float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) * projectors[index].invScreenDims;


	float2 distances = discontinuityDistanceFields[index].SampleLevel(clampSampler, uv, 0);
	float depthMask = saturate(1.f - smoothstep(common.depthHardDistance, common.depthHardDistance + common.depthSmoothDistance, distances.x));
	float colorMask = saturate(1.f - smoothstep(common.colorHardDistance, common.colorHardDistance + common.colorSmoothDistance, distances.y));
	
	//float depthMask = depthMasks[index].SampleLevel(clampSampler, uv, 0); // 1 at edges, 0 everywhere else.
	//float colorMask = colorMasks[index].SampleLevel(clampSampler, uv, 0); // 1 at edges, 0 everywhere else.
	float bestMask = saturate(bestMasks[index].SampleLevel(clampSampler, uv, 0)); // 1 where best, 0 everywhere else.

	float2 distanceFromEdge2 = min(texCoord, dimensions - texCoord);
	float distanceFromEdge = min(distanceFromEdge2.x, distanceFromEdge2.y);
	
	const float edgeWidth = 0.f;
	const float edgeTransition = 100.f;
	float maskFactor = smoothstep(edgeWidth, edgeWidth + edgeTransition, distanceFromEdge);
	float edgeMask = maskFactor;
	
	depthMask *= 1.f;
	colorMask = 1.f - lerp(colorMask * common.colorMaskStrength, 0.f, bestMask);
	
	
	float softMask = min(colorMask, edgeMask);
	float hardMask = 1.f - depthMask;
	
	output[index][texCoord] = saturate(float2(hardMask, softMask));

	//output[index][texCoord] = distances;
}
