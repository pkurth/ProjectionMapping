#include "cs.hlsli"
#include "projector_rs.hlsli"
#include "camera.hlsli"

ConstantBuffer<projector_intensity_cb> cb	: register(b0, space0);
StructuredBuffer<projector_cb> projectors	: register(t0, space0);

Texture2D<float2> confidenceTextures[32]	: register(t0, space1);
Texture2D<float> depthTextures[32]			: register(t0, space2);

RWTexture2D<float> outIntensities[32]		: register(u0, space0);

SamplerState borderSampler					: register(s0);
SamplerState depthSampler					: register(s1);


[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
[RootSignature(PROJECTOR_INTENSITIES_RS)]
void main(cs_input IN)
{
	uint index = cb.index;

	uint2 texCoord = IN.dispatchThreadID.xy;
	float2 dimensions = projectors[index].screenDims;
	if (texCoord.x >= (uint)dimensions.x || texCoord.y >= (uint)dimensions.y)
	{
		return;
	}

	float depth = depthTextures[index][texCoord];
	if (depth == 1.f)
	{
		outIntensities[index][texCoord] = 0.f;
		return;
	}

	float2 invDimensions = projectors[index].invScreenDims;
	float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) * invDimensions;

	float3 P = restoreWorldSpacePosition(projectors[index].invViewProj, uv, depth);


	float Esum = 0.f;
	float k = 4.f;

	uint numProjectors = cb.numProjectors;
	for (uint projIndex = 0; projIndex < numProjectors; ++projIndex)
	{
		if (projIndex != index)
		{
			float4 projected = mul(projectors[projIndex].viewProj, float4(P, 1.f));
			projected.xyz /= projected.w;

			float2 projUV = projected.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
			float testDepth = projected.z;

			float projDepth = depthTextures[projIndex].SampleLevel(depthSampler, projUV, 0);
			if (projDepth < 1.f && testDepth <= projDepth + 0.00005f)
			{
				float atten = confidenceTextures[projIndex].SampleLevel(borderSampler, projUV, 0).x;
				Esum += pow(atten, k);
			}
		}
	}

	float ownAtten = confidenceTextures[index][texCoord].x;
	float E = pow(ownAtten, k);
	float weight = E / max(E + Esum, 0.0001f);

	outIntensities[index][texCoord] = weight / ownAtten;

}
