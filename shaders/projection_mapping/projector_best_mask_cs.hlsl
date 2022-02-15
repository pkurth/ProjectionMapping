#include "cs.hlsli"
#include "projector_rs.hlsli"

#include "camera.hlsli"

ConstantBuffer<projector_best_mask_cb> cb	: register(b0, space0);
StructuredBuffer<projector_cb> projectors	: register(t0, space0);

Texture2D<float3> attenuationTextures[32]	: register(t0, space1);
Texture2D<float> depthTextures[32]			: register(t0, space2);

RWTexture2D<float> outBestMasks[32]			: register(u0, space0);

SamplerState borderSampler					: register(s0);
SamplerState depthSampler					: register(s1);


[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
[RootSignature(PROJECTOR_BEST_MASK_RS)]
void main(cs_input IN)
{
	uint index = cb.index;

	uint2 texCoord = IN.dispatchThreadID.xy;
	float2 dimensions = cb.screenDims;
	if (texCoord.x >= (uint)dimensions.x || texCoord.y >= (uint)dimensions.y)
	{
		return;
	}

	float2 invDimensions = rcp(dimensions);
	float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) * invDimensions;

	float depth = depthTextures[index].SampleLevel(depthSampler, uv, 0);
	if (depth == 1.f)
	{
		outBestMasks[index][texCoord] = 0.f;
		return;
	}

	float3 P = restoreWorldSpacePosition(projectors[index].invViewProj, uv, depth);


	float bestE = 0.f;
	int bestProjectorIndex = -1;

	uint numProjectors = cb.numProjectors;
	for (uint projIndex = 0; projIndex < numProjectors; ++projIndex)
	{
		if (projIndex != index)
		{
			float4 projected = mul(projectors[projIndex].viewProj, float4(P, 1.f));
			projected.xyz /= projected.w;

			float2 projUV = projected.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
			if (all(projUV >= 0.f && projUV <= 1.f))
			{
				float testDepth = projected.z;

				float projDepth = depthTextures[projIndex].SampleLevel(depthSampler, projUV, 0);
				if (projDepth < 1.f && testDepth <= projDepth + 0.00005f)
				{
					float E = attenuationTextures[projIndex].SampleLevel(borderSampler, projUV, 0).y;

					if (E > bestE)
					{
						bestE = E;
						bestProjectorIndex = projIndex;
					}
				}
			}
		}
	}

	float E = attenuationTextures[index].SampleLevel(borderSampler, uv, 0).y;

	float result = 0.f;

	result = E > bestE;

	outBestMasks[index][texCoord] = result;
}
