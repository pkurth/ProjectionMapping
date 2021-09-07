#include "cs.hlsli"
#include "projector_rs.hlsli"
#include "camera.hlsli"
#include "normal.hlsli"

ConstantBuffer<projector_solver_cb> cb		: register(b0, space0);
StructuredBuffer<projector_cb> projectors	: register(t0, space0);

Texture2D<float4> renderResults[]			: register(t0, space1);
Texture2D<float2> worldNormals[]			: register(t0, space2);
Texture2D<float> depthTextures[]			: register(t0, space3);
Texture2D<float> intensities[]				: register(t0, space4);

RWTexture2D<float> outIntensities[]			: register(u0, space0);

SamplerState borderSampler					: register(s0);


[RootSignature(PROJECTOR_SOLVER_RS)]
[numthreads(PROJECTOR_BLOCK_SIZE, PROJECTOR_BLOCK_SIZE, 1)]
void main(cs_input IN)
{
	uint index = cb.currentIndex;

	uint2 texCoord = IN.dispatchThreadID.xy;
	float2 dimensions = projectors[index].screenDims;
	float2 invDimensions = projectors[index].invScreenDims;
	if (texCoord.x >= (uint)dimensions.x || texCoord.y >= (uint)dimensions.y)
	{
		return;
	}

	float2 uv = (float2(texCoord) + float2(0.5f, 0.5f)) * invDimensions;

	uint numProjectors = cb.numProjectors;
	float depth = depthTextures[index][texCoord];
	if (depth == 1.f)
	{
		return;
	}

	float3 worldPosition = restoreWorldSpacePosition(projectors[index].invViewProj, uv, depth);
	float3 N = normalize(unpackNormal(worldNormals[index][texCoord]));


	float intensityByOtherProjectors = 0.f;
	for (uint projIndex = 0; projIndex < numProjectors; ++projIndex)
	{
		if (projIndex != index)
		{
			float4 projected = mul(projectors[projIndex].viewProj, float4(worldPosition, 1.f));
			projected.xyz /= projected.w;

			float2 otherUV = projected.xy * 0.5f + float2(0.5f, 0.5f);
			otherUV.y = 1.f - otherUV.y;
			float testDepth = projected.z;

			float otherDepth = depthTextures[projIndex].SampleLevel(borderSampler, otherUV, 0);
			if (testDepth <= otherDepth + 0.00001f)
			{
				float3 otherV = normalize(projectors[projIndex].position.xyz - worldPosition);
				
				float otherNdotV = saturate(dot(N, otherV)); // Physical intensity.
				float otherIntensity = intensities[projIndex].SampleLevel(borderSampler, otherUV, 0); // Software intensity.

				intensityByOtherProjectors += otherNdotV * otherIntensity;
			}
		}
	}

	float remainingIntensity = 1.f - intensityByOtherProjectors;

	float3 V = normalize(projectors[index].position.xyz - worldPosition);
	float possiblePhysicalIntensity = saturate(dot(N, V));

	float intensity = saturate(remainingIntensity / possiblePhysicalIntensity);

	outIntensities[index][texCoord] = intensity;
}
